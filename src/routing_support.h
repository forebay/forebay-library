#ifndef ROUTING_SUPPORT_H
#define ROUTING_SUPPORT_H

#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "routing_pattern.h"
#include "service_support.h"
#include "spisor/json_kind.h"

/** The cause of a refusal; a NULL return with refused clear means memory ran out. */
typedef struct routing_refusal {
    int refused;
    char cause[320];
    const char *fix;
} routing_refusal;

/** The profile fields a function reads, borrowed from the argument. */
typedef struct routing_profile {
    const char *tier_source_provider;
    const cJSON *tier_order;
    const cJSON *tier_fallback;
    const char *tier_regex;
    const char *native_model_pattern;
    const char *unserved_model_pattern;
} routing_profile;

typedef enum routing_profile_need {
    NEEDS_TIERS = 1,
    NEEDS_NATIVE_PATTERN = 4,
    NEEDS_UNSERVED_PATTERN = 8
} routing_profile_need;

int32_t routing_answer_refusal(const routing_refusal *refusal, uint8_t **out, size_t *out_len);

/** Validates the profile members named by need; on a problem answers 0 with the call already failed. */
int routing_read_profile(const cJSON *profile, int need, routing_profile *read, uint8_t **out, size_t *out_len,
                         int32_t *status);

/** {provider, model, name, derived}, or NULL when memory runs out. */
cJSON *routing_assignment(const char *provider, const char *model, const char *name, int derived);

/** One assignment becomes a chain of one, a list keeps its valid entries. */
cJSON *routing_normalize_chain(const cJSON *raw);

/** Every model of every provider in models.json shape, best first, without the ids the unserved pattern matches (NULL serves all). */
cJSON *routing_catalog(const cJSON *models, const routing_pattern *unserved);

/** The ranking of one provider when it has one, else its model ids in catalog order. */
cJSON *routing_ranked_ids(const cJSON *provider_entry);

/**
 * Compiles source for a profile member, filling refusal when it uses anything the matcher lacks.
 * Answers NULL with refusal.refused set, or NULL with it clear when memory runs out.
 */
routing_pattern *routing_compile_profile_pattern(const char *member, const char *source, int group_required,
                                                 routing_refusal *refusal);

/**
 * Compiles the profile's unservedModelPattern into *pattern, which stays NULL when the profile declares none.
 * Answers 0 with refusal filled, or with it clear when memory runs out.
 */
int routing_compile_unserved_pattern(const routing_profile *profile, routing_pattern **pattern,
                                     routing_refusal *refusal);

/** Whether left and right are equal comparing ASCII letters without regard to case. */
int routing_equals_ignoring_case(const char *left, const char *right);

/** Whether text begins with prefix, comparing ASCII letters without regard to case. */
int routing_starts_with_ignoring_case(const char *text, const char *prefix);

/** The exact key first, then the first key matching ignoring case, as the JavaScript Headers API reads; NULL when absent or not a string. */
const char *routing_header(const cJSON *headers, const char *name);

/** Whether value is an integer from 1 to 65535. */
int routing_is_port(const cJSON *value);

#endif
