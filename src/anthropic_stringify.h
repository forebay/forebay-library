#ifndef ANTHROPIC_STRINGIFY_H
#define ANTHROPIC_STRINGIFY_H

#include <stddef.h>

#include "cJSON.h"

/**
 * The compact JSON of a tree, as JSON.stringify(value) writes it: members in JavaScript's order (array-index keys
 * first, ascending), a number in the shortest form that reads back as the same double, and a number that is not
 * finite as null. Freed with spisor_memory_release; NULL only when memory runs out. `*length` is its length without
 * the NUL.
 */
char *anthropic_stringify(const cJSON *value, size_t *length);

#endif
