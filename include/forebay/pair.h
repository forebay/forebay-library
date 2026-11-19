#ifndef FOREBAY_PUBLIC_PAIR_H
#define FOREBAY_PUBLIC_PAIR_H

/** @file */

#ifdef __cplusplus
extern "C" {
#endif

/** One header or environment variable. */
typedef struct forebay_pair {
    const char *name;
    const char *value;
} forebay_pair;

#ifdef __cplusplus
}
#endif

#endif
