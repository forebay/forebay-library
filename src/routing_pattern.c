#include "routing_pattern.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum node_kind {
    NODE_LITERAL,
    NODE_ANY,
    NODE_CLASS,
    NODE_BEGIN,
    NODE_END,
    NODE_GROUP_OPEN,
    NODE_GROUP_CLOSE
} node_kind;

#define UNBOUNDED (-1)

typedef struct node {
    node_kind kind;
    unsigned char literal;
    uint8_t members[32];
    int negated;
    int minimum;
    int maximum;
} node;

struct routing_pattern {
    node *nodes;
    size_t count;
    size_t capacity;
    int groups;
};

typedef struct capture {
    long start;
    long end;
} capture;

typedef struct parser {
    const char *source;
    size_t position;
    routing_pattern *pattern;
    int inside_group;
    char *reason;
    size_t reason_size;
} parser;

static void set_member(uint8_t *members, unsigned char value) {
    members[value >> 3] = (uint8_t)(members[value >> 3] | (1u << (value & 7)));
}

static int has_member(const uint8_t *members, unsigned char value) {
    return (members[value >> 3] >> (value & 7)) & 1;
}

static void set_range(uint8_t *members, unsigned char first, unsigned char last) {
    for (unsigned int value = first; value <= last; value++) {
        set_member(members, (unsigned char)value);
    }
}

static void add_digits(uint8_t *members) {
    set_range(members, '0', '9');
}

static void add_word(uint8_t *members) {
    add_digits(members);
    set_range(members, 'a', 'z');
    set_range(members, 'A', 'Z');
    set_member(members, '_');
}

static void add_space(uint8_t *members) {
    const char spaces[] = " \t\n\r\f\v";
    for (size_t index = 0; spaces[index] != '\0'; index++) {
        set_member(members, (unsigned char)spaces[index]);
    }
}

static routing_pattern_status unsupported(parser *state, const char *construct) {
    snprintf(state->reason, state->reason_size, "%s", construct);
    return ROUTING_PATTERN_UNSUPPORTED;
}

static node *append_node(parser *state, node_kind kind) {
    routing_pattern *pattern = state->pattern;
    if (pattern->count == pattern->capacity) {
        size_t capacity = pattern->capacity == 0 ? 8 : pattern->capacity * 2;
        node *grown = realloc(pattern->nodes, capacity * sizeof(node));
        if (grown == NULL) {
            return NULL;
        }
        pattern->nodes = grown;
        pattern->capacity = capacity;
    }
    node *added = &pattern->nodes[pattern->count++];
    memset(added, 0, sizeof(*added));
    added->kind = kind;
    added->minimum = 1;
    added->maximum = 1;
    return added;
}

static int is_quantifiable(const node *candidate) {
    return candidate->kind == NODE_LITERAL || candidate->kind == NODE_ANY || candidate->kind == NODE_CLASS;
}

static int is_ascii_letter_or_digit(char value) {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
}

/** Adds the class an escape such as \d names to members, answering 0 when the letter names none. */
static int add_class_escape(char letter, uint8_t *members, int *negated) {
    char lower = (letter >= 'A' && letter <= 'Z') ? (char)(letter + 32) : letter;
    if (lower == 'd') {
        add_digits(members);
    } else if (lower == 'w') {
        add_word(members);
    } else if (lower == 's') {
        add_space(members);
    } else {
        return 0;
    }
    *negated = letter != lower;
    return 1;
}

static int control_escape(char letter) {
    switch (letter) {
    case 'n':
        return '\n';
    case 't':
        return '\t';
    case 'r':
        return '\r';
    default:
        return -1;
    }
}

static routing_pattern_status parse_escape(parser *state) {
    char letter = state->source[state->position++];
    if (letter == '\0') {
        return unsupported(state, "a trailing backslash");
    }
    uint8_t members[32];
    memset(members, 0, sizeof(members));
    int negated = 0;
    node *added = NULL;
    if (add_class_escape(letter, members, &negated)) {
        added = append_node(state, NODE_CLASS);
        if (added == NULL) {
            return ROUTING_PATTERN_OUT_OF_MEMORY;
        }
        memcpy(added->members, members, sizeof(members));
        added->negated = negated;
        return ROUTING_PATTERN_OK;
    }
    int control = control_escape(letter);
    if (control < 0 && is_ascii_letter_or_digit(letter)) {
        char construct[32];
        snprintf(construct, sizeof(construct), "the escape \\%c", letter);
        return unsupported(state, construct);
    }
    if ((unsigned char)letter >= 0x80) {
        return unsupported(state, "a non-ASCII character");
    }
    added = append_node(state, NODE_LITERAL);
    if (added == NULL) {
        return ROUTING_PATTERN_OUT_OF_MEMORY;
    }
    added->literal = (unsigned char)(control >= 0 ? control : letter);
    return ROUTING_PATTERN_OK;
}

static routing_pattern_status parse_class_escape(parser *state, uint8_t *members, int *single, unsigned char *value) {
    char letter = state->source[state->position++];
    int negated = 0;
    *single = 0;
    if (letter == '\0') {
        return unsupported(state, "a trailing backslash");
    }
    if (add_class_escape(letter, members, &negated)) {
        if (negated) {
            return unsupported(state, "a negated escape such as \\D inside a character class");
        }
        return ROUTING_PATTERN_OK;
    }
    int control = control_escape(letter);
    if (control < 0 && is_ascii_letter_or_digit(letter)) {
        char construct[32];
        snprintf(construct, sizeof(construct), "the escape \\%c", letter);
        return unsupported(state, construct);
    }
    if ((unsigned char)letter >= 0x80) {
        return unsupported(state, "a non-ASCII character");
    }
    *single = 1;
    *value = (unsigned char)(control >= 0 ? control : letter);
    return ROUTING_PATTERN_OK;
}

static routing_pattern_status parse_class_member(parser *state, uint8_t *members) {
    unsigned char first = 0;
    int single = 1;
    char current = state->source[state->position];
    if (current == '[') {
        return unsupported(state, "a nested character class");
    }
    if ((unsigned char)current >= 0x80) {
        return unsupported(state, "a non-ASCII character");
    }
    if (current == '\\') {
        state->position++;
        routing_pattern_status status = parse_class_escape(state, members, &single, &first);
        if (status != ROUTING_PATTERN_OK || !single) {
            return status;
        }
    } else {
        first = (unsigned char)current;
        state->position++;
    }
    int is_range = state->source[state->position] == '-' && state->source[state->position + 1] != ']' &&
                   state->source[state->position + 1] != '\0';
    if (!is_range) {
        set_member(members, first);
        return ROUTING_PATTERN_OK;
    }
    state->position++;
    unsigned char last = (unsigned char)state->source[state->position];
    if (last == '\\') {
        state->position++;
        int last_single = 0;
        routing_pattern_status status = parse_class_escape(state, members, &last_single, &last);
        if (status != ROUTING_PATTERN_OK) {
            return status;
        }
        if (!last_single) {
            return unsupported(state, "a class escape as the end of a range");
        }
    } else if (last >= 0x80) {
        return unsupported(state, "a non-ASCII character");
    } else {
        state->position++;
    }
    if (last < first) {
        return unsupported(state, "a character range written backwards");
    }
    set_range(members, first, last);
    return ROUTING_PATTERN_OK;
}

static routing_pattern_status parse_class(parser *state) {
    node *added = append_node(state, NODE_CLASS);
    if (added == NULL) {
        return ROUTING_PATTERN_OUT_OF_MEMORY;
    }
    if (state->source[state->position] == '^') {
        added->negated = 1;
        state->position++;
    }
    size_t start = state->position;
    while (state->source[state->position] != ']') {
        if (state->source[state->position] == '\0') {
            return unsupported(state, "an unterminated character class");
        }
        routing_pattern_status status = parse_class_member(state, state->pattern->nodes[state->pattern->count - 1].members);
        if (status != ROUTING_PATTERN_OK) {
            return status;
        }
    }
    if (state->position == start) {
        return unsupported(state, "an empty character class");
    }
    state->position++;
    return ROUTING_PATTERN_OK;
}

static routing_pattern_status parse_quantifier(parser *state, char symbol) {
    routing_pattern *pattern = state->pattern;
    node *previous = pattern->count == 0 ? NULL : &pattern->nodes[pattern->count - 1];
    if (previous != NULL && previous->kind == NODE_GROUP_CLOSE) {
        return unsupported(state, "a quantified capture group");
    }
    if (previous != NULL && is_quantifiable(previous) && (previous->minimum != 1 || previous->maximum != 1)) {
        if (symbol == '?') {
            return unsupported(state, "a lazy quantifier");
        }
        if (symbol == '+') {
            return unsupported(state, "a possessive quantifier");
        }
        return unsupported(state, "a quantifier on a quantifier");
    }
    if (previous == NULL || !is_quantifiable(previous)) {
        return unsupported(state, "a quantifier with nothing to repeat");
    }
    previous->minimum = symbol == '+' ? 1 : 0;
    previous->maximum = symbol == '?' ? 1 : UNBOUNDED;
    return ROUTING_PATTERN_OK;
}

static routing_pattern_status parse_group_open(parser *state) {
    if (state->source[state->position] == '?') {
        return unsupported(state, "a (?...) group such as a non-capturing group or lookahead");
    }
    if (state->pattern->groups >= 1) {
        return unsupported(state, "more than one capture group");
    }
    state->pattern->groups++;
    state->inside_group = 1;
    return append_node(state, NODE_GROUP_OPEN) == NULL ? ROUTING_PATTERN_OUT_OF_MEMORY : ROUTING_PATTERN_OK;
}

static routing_pattern_status parse_simple(parser *state, node_kind kind) {
    return append_node(state, kind) == NULL ? ROUTING_PATTERN_OUT_OF_MEMORY : ROUTING_PATTERN_OK;
}

static routing_pattern_status parse_literal(parser *state, char value) {
    if ((unsigned char)value >= 0x80) {
        return unsupported(state, "a non-ASCII character");
    }
    node *added = append_node(state, NODE_LITERAL);
    if (added == NULL) {
        return ROUTING_PATTERN_OUT_OF_MEMORY;
    }
    added->literal = (unsigned char)value;
    return ROUTING_PATTERN_OK;
}

static routing_pattern_status parse_group_close(parser *state) {
    if (!state->inside_group) {
        return unsupported(state, "an unbalanced )");
    }
    state->inside_group = 0;
    return parse_simple(state, NODE_GROUP_CLOSE);
}

static routing_pattern_status parse_token(parser *state) {
    char current = state->source[state->position++];
    switch (current) {
    case '^':
        return parse_simple(state, NODE_BEGIN);
    case '$':
        return parse_simple(state, NODE_END);
    case '.':
        return parse_simple(state, NODE_ANY);
    case '(':
        return parse_group_open(state);
    case ')':
        return parse_group_close(state);
    case '[':
        return parse_class(state);
    case '\\':
        return parse_escape(state);
    case '*':
    case '+':
    case '?':
        return parse_quantifier(state, current);
    case '|':
        return unsupported(state, "alternation with |");
    case '{':
        return unsupported(state, "a bounded repetition such as {n,m}");
    default:
        return parse_literal(state, current);
    }
}

routing_pattern_status routing_pattern_compile(const char *source, routing_pattern **pattern, char *reason,
                                               size_t reason_size) {
    *pattern = NULL;
    routing_pattern *compiled = calloc(1, sizeof(*compiled));
    if (compiled == NULL) {
        return ROUTING_PATTERN_OUT_OF_MEMORY;
    }
    parser state = {source, 0, compiled, 0, reason, reason_size};
    routing_pattern_status status = ROUTING_PATTERN_OK;
    while (status == ROUTING_PATTERN_OK && source[state.position] != '\0') {
        status = parse_token(&state);
    }
    if (status == ROUTING_PATTERN_OK && state.inside_group) {
        status = unsupported(&state, "an unbalanced (");
    }
    if (status != ROUTING_PATTERN_OK) {
        routing_pattern_free(compiled);
        return status;
    }
    *pattern = compiled;
    return ROUTING_PATTERN_OK;
}

int routing_pattern_group_count(const routing_pattern *pattern) {
    return pattern->groups;
}

void routing_pattern_free(routing_pattern *pattern) {
    if (pattern != NULL) {
        free(pattern->nodes);
        free(pattern);
    }
}

static size_t sequence_length(unsigned char lead, size_t remaining) {
    size_t length = 1;
    if (lead >= 0xF0) {
        length = 4;
    } else if (lead >= 0xE0) {
        length = 3;
    } else if (lead >= 0xC0) {
        length = 2;
    }
    return length < remaining ? length : remaining;
}

/** Bytes the atom consumes at position, or 0 when it does not match there. */
static size_t step(const node *atom, const char *subject, size_t length, size_t position) {
    if (position >= length) {
        return 0;
    }
    unsigned char current = (unsigned char)subject[position];
    switch (atom->kind) {
    case NODE_LITERAL:
        return current == atom->literal ? 1 : 0;
    case NODE_ANY:
        return (current == '\n' || current == '\r') ? 0 : sequence_length(current, length - position);
    case NODE_CLASS:
        if (atom->negated) {
            return (current < 0x80 && has_member(atom->members, current)) ? 0
                                                                           : sequence_length(current, length - position);
        }
        return (current < 0x80 && has_member(atom->members, current)) ? 1 : 0;
    default:
        return 0;
    }
}

static size_t previous_boundary(const char *subject, size_t position) {
    position--;
    while (position > 0 && (((unsigned char)subject[position]) & 0xC0) == 0x80) {
        position--;
    }
    return position;
}

static int match_from(const routing_pattern *pattern, size_t index, const char *subject, size_t length,
                      size_t position, capture *group);

static int match_repeated(const routing_pattern *pattern, size_t index, const char *subject, size_t length,
                          size_t position, capture *group) {
    const node *atom = &pattern->nodes[index];
    int taken = 0;
    size_t end = position;
    while (atom->maximum == UNBOUNDED || taken < atom->maximum) {
        size_t consumed = step(atom, subject, length, end);
        if (consumed == 0) {
            break;
        }
        end += consumed;
        taken++;
    }
    for (;;) {
        if (taken >= atom->minimum && match_from(pattern, index + 1, subject, length, end, group)) {
            return 1;
        }
        if (taken <= atom->minimum) {
            return 0;
        }
        end = previous_boundary(subject, end);
        taken--;
    }
}

static int match_end(const char *subject, size_t length, size_t position) {
    return position == length || (position + 1 == length && subject[position] == '\n');
}

static int match_from(const routing_pattern *pattern, size_t index, const char *subject, size_t length,
                      size_t position, capture *group) {
    if (index == pattern->count) {
        return 1;
    }
    const node *current = &pattern->nodes[index];
    long saved;
    int matched;
    switch (current->kind) {
    case NODE_BEGIN:
        return position == 0 && match_from(pattern, index + 1, subject, length, position, group);
    case NODE_END:
        return match_end(subject, length, position) && match_from(pattern, index + 1, subject, length, position, group);
    case NODE_GROUP_OPEN:
        saved = group->start;
        group->start = (long)position;
        matched = match_from(pattern, index + 1, subject, length, position, group);
        if (!matched) {
            group->start = saved;
        }
        return matched;
    case NODE_GROUP_CLOSE:
        saved = group->end;
        group->end = (long)position;
        matched = match_from(pattern, index + 1, subject, length, position, group);
        if (!matched) {
            group->end = saved;
        }
        return matched;
    default:
        return match_repeated(pattern, index, subject, length, position, group);
    }
}

int routing_pattern_find(const routing_pattern *pattern, const char *subject, size_t *group_start,
                         size_t *group_length) {
    size_t length = strlen(subject);
    for (size_t start = 0; start <= length; start++) {
        capture group = {-1, -1};
        if (match_from(pattern, 0, subject, length, start, &group)) {
            if (group_start != NULL && group_length != NULL && group.start >= 0 && group.end >= group.start) {
                *group_start = (size_t)group.start;
                *group_length = (size_t)(group.end - group.start);
            }
            return 1;
        }
    }
    return 0;
}
