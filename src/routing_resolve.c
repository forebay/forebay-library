#include "routing_resolve.h"

#include <string.h>

#define DEFAULT_SLOT "default"
#define NOTICE_SWITCHED "model_switched"
#define NOTICE_HEALED "healed"

typedef struct resolution {
    const cJSON *chain;
    cJSON *owned_chain;
    int switched;
    int healed;
} resolution;

static cJSON *normalized_map(const cJSON *map) {
    cJSON *normalized = cJSON_CreateObject();
    const cJSON *chain = NULL;
    if (normalized == NULL) {
        return NULL;
    }
    cJSON_ArrayForEach(chain, map) {
        if (!routing_add(normalized, chain->string, routing_normalize_chain(chain))) {
            cJSON_Delete(normalized);
            return NULL;
        }
    }
    return normalized;
}

static const cJSON *chain_serving_model(const cJSON *map, const char *requested) {
    const cJSON *chain = NULL;
    cJSON_ArrayForEach(chain, map) {
        const cJSON *assignment = NULL;
        cJSON_ArrayForEach(assignment, chain) {
            if (strcmp(cJSON_GetObjectItemCaseSensitive(assignment, "model")->valuestring, requested) == 0) {
                return chain;
            }
        }
    }
    return NULL;
}

static const char *slot_for_model(const cJSON *map, const char *requested) {
    const cJSON *chain = NULL;
    cJSON_ArrayForEach(chain, map) {
        if (strcmp(chain->string, DEFAULT_SLOT) != 0 && routing_contains_ignoring_case(requested, chain->string)) {
            return chain->string;
        }
    }
    return DEFAULT_SLOT;
}

static const cJSON *catalog_entry_for(const cJSON *catalog, const char *requested) {
    const cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, catalog) {
        if (strcmp(cJSON_GetObjectItemCaseSensitive(entry, "model")->valuestring, requested) == 0) {
            return entry;
        }
    }
    return NULL;
}

static cJSON *single_chain(const cJSON *entry) {
    cJSON *chain = cJSON_CreateArray();
    if (chain == NULL || !routing_push(chain, routing_assignment(
                                                  cJSON_GetObjectItemCaseSensitive(entry, "provider")->valuestring,
                                                  cJSON_GetObjectItemCaseSensitive(entry, "model")->valuestring,
                                                  cJSON_GetObjectItemCaseSensitive(entry, "name")->valuestring, 0))) {
        cJSON_Delete(chain);
        return NULL;
    }
    return chain;
}

static int matches_native(const routing_pattern *native, const char *requested) {
    return native != NULL && routing_pattern_find(native, requested, NULL, NULL);
}

static const cJSON *slot_chain_or_default(const cJSON *map, const char *slot) {
    const cJSON *chain = cJSON_GetObjectItemCaseSensitive(map, slot);
    if (cJSON_GetArraySize(chain) > 0) {
        return chain;
    }
    return cJSON_GetObjectItemCaseSensitive(map, DEFAULT_SLOT);
}

static int primary_is_derived(const cJSON *chain) {
    return chain != NULL && chain->child != NULL &&
           cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(chain->child, "derived"));
}

/**
 * The four steps in order: a chain already serving the exact model id; the first tier whose name
 * the id contains; a model a provider offers, served as itself; the default chain.
 *
 * @implNote Reaching the default tier for a model nobody offers is announced as a switch unless
 * the profile says the id is the application's own.
 */
static int resolve_chain(const cJSON *map, const cJSON *catalog, const char *requested,
                         const routing_pattern *native, resolution *resolved) {
    resolved->chain = NULL;
    resolved->owned_chain = NULL;
    resolved->switched = 0;
    resolved->healed = 0;
    if (requested[0] != '\0') {
        resolved->chain = chain_serving_model(map, requested);
    }
    if (resolved->chain == NULL) {
        const char *slot = slot_for_model(map, requested);
        if (strcmp(slot, DEFAULT_SLOT) == 0 && requested[0] != '\0') {
            const cJSON *offered = catalog_entry_for(catalog, requested);
            if (offered != NULL) {
                resolved->owned_chain = single_chain(offered);
                resolved->chain = resolved->owned_chain;
                return resolved->owned_chain != NULL;
            }
            if (!matches_native(native, requested)) {
                resolved->switched = 1;
            }
        }
        resolved->chain = slot_chain_or_default(map, slot);
    }
    resolved->healed = primary_is_derived(resolved->chain);
    return 1;
}

static int add_notices(cJSON *result, const resolution *resolved) {
    cJSON *notices = cJSON_CreateArray();
    if (notices == NULL) {
        return 0;
    }
    if ((resolved->switched && !routing_push(notices, cJSON_CreateString(NOTICE_SWITCHED))) ||
        (resolved->healed && !routing_push(notices, cJSON_CreateString(NOTICE_HEALED)))) {
        cJSON_Delete(notices);
        return 0;
    }
    return routing_add(result, "notices", notices);
}

static cJSON *answer_for(const resolution *resolved) {
    cJSON *result = cJSON_CreateObject();
    cJSON *chain = resolved->chain == NULL ? cJSON_CreateArray() : cJSON_Duplicate(resolved->chain, 1);
    if (result == NULL || !routing_add(result, "chain", chain) || !add_notices(result, resolved)) {
        cJSON_Delete(result);
        return NULL;
    }
    return result;
}

static int32_t answer_call(const cJSON *profile_json, const cJSON *map_json, const cJSON *models,
                           const char *requested, uint8_t **out, size_t *out_len) {
    int32_t status = SPISOR_STATUS_SUCCESS;
    routing_profile profile;
    if (!routing_read_profile(profile_json, NEEDS_NATIVE_PATTERN, &profile, out, out_len, &status)) {
        return status;
    }
    routing_refusal refusal = {0, "", NULL};
    routing_pattern *native = NULL;
    if (profile.native_model_pattern != NULL && profile.native_model_pattern[0] != '\0') {
        native = routing_compile_profile_pattern("nativeModelPattern", profile.native_model_pattern, 0, &refusal);
        if (native == NULL) {
            return routing_answer_refusal(&refusal, out, out_len);
        }
    }
    cJSON *map = normalized_map(map_json);
    cJSON *catalog = routing_catalog(models);
    resolution resolved;
    cJSON *result = NULL;
    if (map != NULL && catalog != NULL && resolve_chain(map, catalog, requested, native, &resolved)) {
        result = answer_for(&resolved);
        cJSON_Delete(resolved.owned_chain);
    }
    status = routing_answer(result, out, out_len);
    cJSON_Delete(map);
    cJSON_Delete(catalog);
    routing_pattern_free(native);
    return status;
}

ROUTING_EXPORT int32_t routing_resolve(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len) {
    spisor_call call;
    int32_t status = routing_open(in, in_len, &call, out, out_len);
    if (status != SPISOR_STATUS_SUCCESS) {
        return status;
    }
    const cJSON *profile = NULL;
    const cJSON *map = NULL;
    const cJSON *models = NULL;
    const cJSON *requested = NULL;
    if (routing_require(&call, "profile", routing_is_object, "an object", &profile, out, out_len, &status) &&
        routing_require(&call, "map", routing_is_object, "an object", &map, out, out_len, &status) &&
        routing_require(&call, "models", routing_is_object, "an object", &models, out, out_len, &status) &&
        routing_require(&call, "requestedModel", routing_is_string, "a string", &requested, out, out_len,
                        &status)) {
        status = answer_call(profile, map, models, requested->valuestring, out, out_len);
    }
    spisor_discard_call(&call);
    return status;
}
