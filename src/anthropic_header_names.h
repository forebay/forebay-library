#ifndef ANTHROPIC_HEADER_NAMES_H
#define ANTHROPIC_HEADER_NAMES_H

/** `letter` in lower case when it is an ASCII capital, else unchanged, whatever the C locale. */
static inline char anthropic_ascii_lower(char letter) {
    return letter >= 'A' && letter <= 'Z' ? (char)(letter - 'A' + 'a') : letter;
}

/** Whether two header names are the same ignoring ASCII case. */
static inline int anthropic_names_match(const char *left, const char *right) {
    for (; *left != '\0' && *right != '\0'; left++, right++) {
        if (anthropic_ascii_lower(*left) != anthropic_ascii_lower(*right)) {
            return 0;
        }
    }
    return *left == *right;
}

#endif
