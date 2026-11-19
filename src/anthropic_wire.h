#ifndef ANTHROPIC_WIRE_H
#define ANTHROPIC_WIRE_H

#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"

/**
 * Lenient reads of a parsed wire body, each treating a member of the wrong type as absent, as the translator's
 * `asString` and the others do. A member read is NULL or 0 when it is absent, JSON null or of another type.
 */

/** The member of an object, NULL when absent. */
const cJSON *anthropic_wire_member(const cJSON *object, const char *key);

/** The text of a JSON string, else NULL. */
const char *anthropic_wire_string(const cJSON *item);

/** The item when it is a JSON object, else NULL. */
const cJSON *anthropic_wire_map(const cJSON *item);

/** The item when it is a JSON array, else NULL. */
const cJSON *anthropic_wire_list(const cJSON *item);

/** Whether the item is a boolean, setting `*value`. */
int anthropic_wire_boolean(const cJSON *item, uint32_t *value);

/** Whether the item is a number whose integer part is a count from 0 to 2^53, setting `*value` to that part. */
int anthropic_wire_integer(const cJSON *item, uint64_t *value);

/** Whether the item is a number, setting `*value`. */
int anthropic_wire_number(const cJSON *item, double *value);

/** How many items of an array are objects. */
size_t anthropic_wire_map_count(const cJSON *list);

/** The next object of an array after `previous`, the first when it is NULL; NULL after the last. */
const cJSON *anthropic_wire_next_map(const cJSON *list, const cJSON *previous);

#endif
