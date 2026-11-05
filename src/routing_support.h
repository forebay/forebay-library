#ifndef ROUTING_SUPPORT_H
#define ROUTING_SUPPORT_H

#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "routing_pattern.h"
#include "spisor/json_kind.h"

#if defined(_WIN32)
#define ROUTING_EXPORT __declspec(dllexport)
#define ROUTING_WINDOWS 1
#else
#define ROUTING_EXPORT __attribute__((visibility("default")))
#define ROUTING_WINDOWS 0
#endif

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
    const char *env_prefix;
    const char *native_model_pattern;
} routing_profile;

typedef enum routing_profile_need {
    NEEDS_TIERS = 1,
    NEEDS_ENV_PREFIX = 2,
    NEEDS_NATIVE_PATTERN = 4
} routing_profile_need;

int32_t routing_out_of_memory(uint8_t **out, size_t *out_len);

/** A semantic refusal, answered as a successful call: {"refused": cause, "fix": fix}. */
int32_t routing_refuse(const char *cause, const char *fix, uint8_t **out, size_t *out_len);

int32_t routing_answer_refusal(const routing_refusal *refusal, uint8_t **out, size_t *out_len);

/** Answers result and consumes it; NULL answers out of memory. */
int32_t routing_answer(cJSON *result, uint8_t **out, size_t *out_len);

/** Fails a malformed call as "<subject> <problem>", the standard failure object. */
int32_t routing_fail(const char *subject, const char *problem, const char *fix, uint8_t **out, size_t *out_len);

/** Parses a plain call whose argument must be an object. */
int32_t routing_open(const uint8_t *in, size_t in_len, spisor_call *call, uint8_t **out, size_t *out_len);

/** Reads the named argument member of the type asked for; answers 0 with the call already failed. */
int routing_require(const spisor_call *call, const char *name, int (*is_type)(const cJSON *), const char *type_name,
                    const cJSON **member, uint8_t **out, size_t *out_len, int32_t *status);

/** Validates the profile members named by need; on a problem answers 0 with the call already failed. */
int routing_read_profile(const cJSON *profile, int need, routing_profile *read, uint8_t **out, size_t *out_len,
                         int32_t *status);

/** {name: item} with item consumed, or NULL when memory runs out. */
cJSON *routing_wrap(const char *name, cJSON *item);

int routing_add(cJSON *object, const char *name, cJSON *item);
int routing_push(cJSON *array, cJSON *item);

/** {provider, model, name, derived}, or NULL when memory runs out. */
cJSON *routing_assignment(const char *provider, const char *model, const char *name, int derived);

/** basekit's normalizeChain: one assignment becomes a chain of one, a list keeps its valid entries. */
cJSON *routing_normalize_chain(const cJSON *raw);

/** Every model of every provider in models.json shape, best first, without "-auto" ids. */
cJSON *routing_catalog(const cJSON *models);

/** The ranking of one provider when it has one, else its model ids in catalog order. */
cJSON *routing_ranked_ids(const cJSON *provider_entry);

int routing_is_auto(const char *model);
int routing_contains_ignoring_case(const char *haystack, const char *needle);
int routing_is_object(const cJSON *value);
int routing_is_array(const cJSON *value);
int routing_is_string(const cJSON *value);
int routing_is_number(const cJSON *value);
int routing_is_boolean(const cJSON *value);

/**
 * Compiles source for a profile member, filling refusal when it uses anything the matcher lacks.
 * Answers NULL with refusal.refused set, or NULL with it clear when memory runs out.
 */
routing_pattern *routing_compile_profile_pattern(const char *member, const char *source, int group_required,
                                                 routing_refusal *refusal);

#endif
