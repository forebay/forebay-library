#include "routing_tiers.h"

#include <stdlib.h>
#include <string.h>

#define UNKNOWN_TIER_RANK 99

typedef struct tier {
    const char *name;
    int rank;
} tier;

static int rank_of(const cJSON *tier_order, const char *name) {
    int index = 0;
    const cJSON *known = NULL;
    cJSON_ArrayForEach(known, tier_order) {
        if (strcmp(known->valuestring, name) == 0) {
            return index;
        }
        index++;
    }
    return UNKNOWN_TIER_RANK;
}

static int compare_tiers(const void *left, const void *right) {
    const tier *first = left;
    const tier *second = right;
    if (first->rank != second->rank) {
        return first->rank < second->rank ? -1 : 1;
    }
    return strcmp(first->name, second->name);
}

static int already_listed(const cJSON *tiers, const char *name) {
    const cJSON *listed = NULL;
    cJSON_ArrayForEach(listed, tiers) {
        if (strcmp(listed->valuestring, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int collect_tier(cJSON *tiers, const routing_pattern *pattern, const char *id) {
    size_t start = 0;
    size_t length = 0;
    if (!routing_pattern_find(pattern, id, &start, &length)) {
        return 1;
    }
    char *name = malloc(length + 1);
    if (name == NULL) {
        return 0;
    }
    memcpy(name, id + start, length);
    name[length] = '\0';
    int ok = already_listed(tiers, name) || service_push(tiers, cJSON_CreateString(name));
    free(name);
    return ok;
}

static cJSON *sorted_copy(const cJSON *tiers, const cJSON *tier_order) {
    int count = cJSON_GetArraySize(tiers);
    tier *entries = malloc(sizeof(tier) * (size_t)(count == 0 ? 1 : count));
    cJSON *sorted = cJSON_CreateArray();
    if (entries == NULL || sorted == NULL) {
        free(entries);
        cJSON_Delete(sorted);
        return NULL;
    }
    int index = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, tiers) {
        entries[index].name = item->valuestring;
        entries[index].rank = rank_of(tier_order, item->valuestring);
        index++;
    }
    qsort(entries, (size_t)count, sizeof(tier), compare_tiers);
    for (index = 0; index < count; index++) {
        if (!service_push(sorted, cJSON_CreateString(entries[index].name))) {
            cJSON_Delete(sorted);
            sorted = NULL;
            break;
        }
    }
    free(entries);
    return sorted;
}

static cJSON *detect_with(const routing_pattern *pattern, const routing_profile *profile, const cJSON *models) {
    cJSON *tiers = cJSON_CreateArray();
    cJSON *ids = routing_ranked_ids(cJSON_GetObjectItemCaseSensitive(models, profile->tier_source_provider));
    if (tiers == NULL || ids == NULL) {
        cJSON_Delete(tiers);
        cJSON_Delete(ids);
        return NULL;
    }
    const cJSON *id = NULL;
    cJSON_ArrayForEach(id, ids) {
        if (!collect_tier(tiers, pattern, id->valuestring)) {
            cJSON_Delete(tiers);
            cJSON_Delete(ids);
            return NULL;
        }
    }
    cJSON_Delete(ids);
    cJSON *result = cJSON_GetArraySize(tiers) == 0 ? cJSON_Duplicate(profile->tier_fallback, 1)
                                                   : sorted_copy(tiers, profile->tier_order);
    cJSON_Delete(tiers);
    return result;
}

cJSON *routing_detect_tiers(const routing_profile *profile, const cJSON *models, routing_refusal *refusal) {
    routing_pattern *pattern = routing_compile_profile_pattern("tierRegex", profile->tier_regex, 1, refusal);
    if (pattern == NULL) {
        return NULL;
    }
    cJSON *tiers = detect_with(pattern, profile, models);
    routing_pattern_free(pattern);
    refusal->refused = 0;
    return tiers;
}

SERVICE_EXPORT int32_t forebay_library_routing_tiers(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len) {
    spisor_call call;
    int32_t status = service_open(in, in_len, &call, out, out_len);
    if (status != SPISOR_STATUS_SUCCESS) {
        return status;
    }
    const cJSON *profile_json = NULL;
    const cJSON *models = NULL;
    routing_profile profile;
    if (service_require(&call, "profile", service_is_object, "an object", &profile_json, out, out_len, &status) &&
        service_require(&call, "models", service_is_object, "an object", &models, out, out_len, &status) &&
        routing_read_profile(profile_json, NEEDS_TIERS, &profile, out, out_len, &status)) {
        routing_refusal refusal = {0, "", NULL};
        cJSON *tiers = routing_detect_tiers(&profile, models, &refusal);
        if (tiers == NULL) {
            status = routing_answer_refusal(&refusal, out, out_len);
        } else {
            status = service_answer(service_wrap("tiers", tiers), out, out_len);
        }
    }
    spisor_discard_call(&call);
    return status;
}
