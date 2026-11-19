#ifndef ROUTING_ARGUMENTS_H
#define ROUTING_ARGUMENTS_H

#include "cJSON.h"
#include "forebay/routing.h"

/**
 * @file
 * The checks a routing definition makes of the caller's structs before it allocates anything, and the cJSON trees
 * routing's decisions read, built from structs that passed them. Every builder answers NULL when memory runs out.
 */

/** Whether `count` texts are each present, at an array present unless the count is 0. */
int routing_are_texts(const char *const *texts, size_t count);

/** Whether `count` pairs each carry a name and a value. */
int routing_are_pairs(const forebay_pair *pairs, size_t count);

/** Whether `count` slots each carry a name and a chain whose assignments carry a provider and a model. */
int routing_are_slots(const forebay_slot *slots, size_t count);

/** Whether `count` catalog providers each carry an id, models with ids, and texts for a ranking. */
int routing_is_catalog(const forebay_catalog_provider *providers, size_t count);

/** Whether a profile reaches through its first shape, and carries the tier members when `needs_tiers` is 1. */
int routing_is_profile(const forebay_routing_profile *profile, int needs_tiers);

/** Whether an array is present unless its count is 0. */
#define ROUTING_IS_ARRAY(items, count) ((items) != NULL || (count) == 0)

/** {name: value}, a later pair of a name replacing an earlier one. */
cJSON *routing_pairs_json(const forebay_pair *pairs, size_t count);

cJSON *routing_texts_json(const char *const *texts, size_t count);

/** {slot: [{provider, model, name, derived}]}. */
cJSON *routing_slots_json(const forebay_slot *slots, size_t count);

/** {provider: {models: {id: {name}}, ranking}}. */
cJSON *routing_catalog_json(const forebay_catalog_provider *providers, size_t count);

/** The profile members routing reads, each pattern left out when it is NULL. */
cJSON *routing_profile_json(const forebay_routing_profile *profile);

/** Adds `item` under `name`, deleting it when it cannot; 0 for a NULL item or memory running out. */
int routing_add_json(cJSON *object, const char *name, cJSON *item);

#endif
