#include <stdio.h>
#include <string.h>

#include "routing_pattern.h"

static int failures = 0;

static void fail(const char *label, const char *detail) {
    fprintf(stderr, "FAIL %s: %s\n", label, detail);
    failures++;
}

static routing_pattern *compile_ok(const char *source) {
    routing_pattern *pattern = NULL;
    char reason[160];
    if (routing_pattern_compile(source, &pattern, reason, sizeof(reason)) != ROUTING_PATTERN_OK) {
        fail(source, reason);
        return NULL;
    }
    return pattern;
}

static void expect_group(const char *source, const char *subject, const char *expected) {
    routing_pattern *pattern = compile_ok(source);
    if (pattern == NULL) {
        return;
    }
    size_t start = 0;
    size_t length = 0;
    int found = routing_pattern_find(pattern, subject, &start, &length);
    if (expected == NULL) {
        if (found) {
            fail(source, "matched where no match was expected");
        }
    } else if (!found || strlen(expected) != length || strncmp(subject + start, expected, length) != 0) {
        char detail[200];
        snprintf(detail, sizeof(detail), "on \"%s\" expected group \"%s\", found=%d length=%zu", subject, expected,
                 found, length);
        fail(source, detail);
    }
    routing_pattern_free(pattern);
}

static void expect_match(const char *source, const char *subject, int expected) {
    routing_pattern *pattern = compile_ok(source);
    if (pattern == NULL) {
        return;
    }
    if (routing_pattern_find(pattern, subject, NULL, NULL) != expected) {
        char detail[200];
        snprintf(detail, sizeof(detail), "on \"%s\" expected match=%d", subject, expected);
        fail(source, detail);
    }
    routing_pattern_free(pattern);
}

static void expect_unsupported(const char *source, const char *construct) {
    routing_pattern *pattern = NULL;
    char reason[160] = "";
    routing_pattern_status status = routing_pattern_compile(source, &pattern, reason, sizeof(reason));
    if (status != ROUTING_PATTERN_UNSUPPORTED || strstr(reason, construct) == NULL) {
        char detail[240];
        snprintf(detail, sizeof(detail), "expected a refusal naming \"%s\", got status %d reason \"%s\"", construct,
                 (int)status, reason);
        fail(source, detail);
    }
    routing_pattern_free(pattern);
}

static void captures_the_family_token(void) {
    expect_group("^claude-([a-z]+)-\\d", "claude-opus-4-6-thinking", "opus");
    expect_group("^claude-([a-z]+)-\\d", "claude-fable-5", "fable");
    expect_group("^claude-([a-z]+)-\\d", "gemini-3-pro", NULL);
    expect_group("^claude-([a-z]+)-\\d", "claude-opus-x", NULL);
    expect_group("^claude-([a-z]+)-\\d", "xclaude-opus-4", NULL);
}

static void searches_anywhere_when_unanchored(void) {
    expect_group("claude-([a-z]+)", "my-claude-haiku-9", "haiku");
    expect_match("opus", "claude-opus-4", 1);
    expect_match("^opus", "claude-opus-4", 0);
    expect_match("4$", "claude-opus-4", 1);
    expect_match("opus$", "claude-opus-4", 0);
}

static void backtracks_greedy_quantifiers(void) {
    expect_group("^(.*)-\\d", "claude-opus-4-6", "claude-opus-4");
    expect_group("^(a*)ab", "aaab", "aa");
    expect_group("^(a+)a", "a", NULL);
    expect_group("^(a?)a", "a", "");
    expect_group("^x(\\d+)$", "x2026", "2026");
}

static void reads_classes(void) {
    expect_group("^([a-c]+)", "abcd", "abc");
    expect_group("^([^-]+)-", "claude-opus", "claude");
    expect_group("^([\\w.]+)", "gpt-4.1", "gpt");
    expect_group("^([a-z0-9_]+)", "o3_mini-x", "o3_mini");
    expect_group("^(\\D+)", "abc1", "abc");
    expect_group("^(\\s*)x", "  x", "  ");
    expect_group("^([a\\-z]+)", "a-z!", "a-z");
    expect_group("^(\\.)", ".x", ".");
}

static void never_splits_a_utf8_character(void) {
    expect_group("^(.)", "\xC3\xA9x", "\xC3\xA9");
    expect_group("^([^x]+)x", "\xE2\x82\xAC" "ax", "\xE2\x82\xAC" "a");
    expect_match("^.$", "\xF0\x9F\x98\x80", 1);
}

static void never_starts_inside_a_utf8_character(void) {
    expect_group("(.).a", "\xE2\x82\xAC" "a", NULL);
    expect_match(".*x", "\xE2\x82\xAC", 0);
    expect_match(".*x", "\xE2\x82\xAC" "x", 1);
    expect_group("(.)a", "\xE2\x82\xAC" "a", "\xE2\x82\xAC");
}

static void refuses_what_it_cannot_match_faithfully(void) {
    expect_unsupported("a|b", "alternation");
    expect_unsupported("(a)(b)", "more than one capture group");
    expect_unsupported("(?:a)", "(?...)");
    expect_unsupported("(a)+", "quantified capture group");
    expect_unsupported("a{2}", "bounded repetition");
    expect_unsupported("a+?", "lazy quantifier");
    expect_unsupported("a++", "possessive quantifier");
    expect_unsupported("a**", "quantifier on a quantifier");
    expect_unsupported("*a", "nothing to repeat");
    expect_unsupported("^*", "nothing to repeat");
    expect_unsupported("\\bword", "\\b");
    expect_unsupported("(\\1)", "\\1");
    expect_unsupported("[a-", "unterminated");
    expect_unsupported("[]", "empty character class");
    expect_unsupported("[b-a]", "backwards");
    expect_unsupported("[[a]]", "nested");
    expect_unsupported("(a", "unbalanced (");
    expect_unsupported("a)", "unbalanced )");
    expect_unsupported("caf\xC3\xA9", "non-ASCII");
    expect_unsupported("a\\", "trailing backslash");
}

static void counts_its_groups(void) {
    routing_pattern *with_group = compile_ok("^a(b)c");
    routing_pattern *without_group = compile_ok("^abc");
    if (with_group == NULL || without_group == NULL) {
        return;
    }
    if (routing_pattern_group_count(with_group) != 1 || routing_pattern_group_count(without_group) != 0) {
        fail("group count", "expected 1 and 0");
    }
    routing_pattern_free(with_group);
    routing_pattern_free(without_group);
}

int main(void) {
    captures_the_family_token();
    searches_anywhere_when_unanchored();
    backtracks_greedy_quantifiers();
    reads_classes();
    never_splits_a_utf8_character();
    never_starts_inside_a_utf8_character();
    refuses_what_it_cannot_match_faithfully();
    counts_its_groups();
    if (failures == 0) {
        printf("routing_pattern_test: all checks passed\n");
        return 0;
    }
    fprintf(stderr, "routing_pattern_test: %d checks failed\n", failures);
    return 1;
}
