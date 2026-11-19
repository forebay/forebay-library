#ifndef ROUTING_ANSWERS_H
#define ROUTING_ANSWERS_H

#include "cJSON.h"
#include "forebay/routing.h"
#include "service_support.h"

/**
 * @file
 * A routing decision's cJSON result read into a definition's answer, every text and array owned by the answer's
 * storage chain. A reader answers 1, or 0 when memory runs out, the blocks it took left in the chain for the caller
 * to release with the rest.
 */

/** An array of strings. */
int routing_read_texts(void **storage, const cJSON *json, const char *const **texts, size_t *count);

/** An object of string members as pairs, in member order. */
int routing_read_pairs(void **storage, const cJSON *json, const forebay_pair **pairs, size_t *count);

/** An array of {provider, model, name, derived}. */
int routing_read_assignments(void **storage, const cJSON *json, const forebay_assignment **chain, size_t *count);

/** An object of chains as slots, in member order. */
int routing_read_slots(void **storage, const cJSON *json, const forebay_slot **slots, size_t *count);

/** An array of {level, message}. */
int routing_read_log(void **storage, const cJSON *json, const forebay_log_line **log, size_t *count);

/** A text member, NULL when absent. */
int routing_read_text(void **storage, const cJSON *object, const char *name, const char **text);

/** A reply's `kind` as a forebay_reply_kind, 0 for none. */
uint32_t routing_reply_kind(const cJSON *reply);

/** A number member, 0 when absent. */
double routing_number(const cJSON *object, const char *name);

#endif
