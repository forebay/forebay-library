#ifndef ANTHROPIC_HEADER_NAMES_H
#define ANTHROPIC_HEADER_NAMES_H

#include <ctype.h>

/** Whether two header names are the same ignoring ASCII case. */
static inline int anthropic_names_match(const char *left, const char *right) {
    for (; *left != '\0' && *right != '\0'; left++, right++) {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right)) {
            return 0;
        }
    }
    return *left == *right;
}

#endif
