#ifndef FOREBAY_ANSWER_STORAGE_H
#define FOREBAY_ANSWER_STORAGE_H

#include <stddef.h>

/**
 * An answer's storage: a chain of blocks its `storage` field heads, each from the binary's one allocator, freed
 * together by forebay_storage_release. A fill that runs out of memory releases the chain, so a failed answer holds
 * nothing.
 */

/** `bytes` zeroed bytes owned by `*storage`, aligned for any type; NULL when memory runs out. */
void *forebay_storage_allocate(void **storage, size_t bytes);

/** `count` elements of `size` bytes owned by `*storage`; NULL for a count of 0, and when memory runs out. */
void *forebay_storage_array(void **storage, size_t count, size_t size);

/**
 * A copy of `text` owned by `*storage` in `*copy`, NULL for a NULL text.
 *
 * @return 1, or 0 when memory runs out.
 */
int forebay_storage_text(void **storage, const char *text, const char **copy);

/** Frees every block of a chain; NULL does nothing. */
void forebay_storage_release(void *storage);

#endif
