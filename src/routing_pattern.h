#ifndef ROUTING_PATTERN_H
#define ROUTING_PATTERN_H

#include <stddef.h>

typedef struct routing_pattern routing_pattern;

typedef enum routing_pattern_status {
    ROUTING_PATTERN_OK,
    ROUTING_PATTERN_UNSUPPORTED,
    ROUTING_PATTERN_OUT_OF_MEMORY
} routing_pattern_status;

/**
 * Compiles the subset of regular expression syntax the routing profiles use: ^ and $, literals,
 * ., [...] and [^...] with ranges, \d \w \s and their negations, * + ? on a single atom, and at
 * most one capture group.
 *
 * @implNote One matcher serves every platform because Windows has no regcomp. Anything else is
 * refused with a reason naming the construct, never read as something close to it. ASCII only, and
 * . and negated classes consume a whole UTF-8 sequence so a capture can never split a character.
 */
routing_pattern_status routing_pattern_compile(const char *source, routing_pattern **pattern, char *reason,
                                               size_t reason_size);

int routing_pattern_group_count(const routing_pattern *pattern);

/** Searches anywhere in the subject; on a match the first group's span is written when requested. */
int routing_pattern_find(const routing_pattern *pattern, const char *subject, size_t *group_start,
                         size_t *group_length);

void routing_pattern_free(routing_pattern *pattern);

#endif
