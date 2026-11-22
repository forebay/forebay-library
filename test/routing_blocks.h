#ifndef FOREBAY_TEST_ROUTING_BLOCKS_H
#define FOREBAY_TEST_ROUTING_BLOCKS_H

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "forebay/routing.h"

/**
 * @file
 * Test-only reading of routing's JSON shapes into its structs, every array taken from one list of blocks that
 * routing_blocks_release frees together, so a vector's structs live until the vector is done.
 */

typedef union routing_block {
    union routing_block *next;
    long double alignment;
} routing_block;

static routing_block *routing_blocks = NULL;

static inline void *routing_blocks_take(size_t bytes) {
    routing_block *block = calloc(1, sizeof(routing_block) + bytes);
    if (block == NULL) {
        abort();
    }
    block->next = routing_blocks;
    routing_blocks = block;
    return block + 1;
}

static inline void routing_blocks_release(void) {
    while (routing_blocks != NULL) {
        routing_block *next = routing_blocks->next;
        free(routing_blocks);
        routing_blocks = next;
    }
}

static inline const char *routing_text_of(const cJSON *object, const char *name) {
    return cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(object, name));
}

/** A member that is a number as a count, 0 for any other member or none. */
static inline uint64_t routing_count_of(const cJSON *object, const char *name) {
    const cJSON *member = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsNumber(member) && member->valuedouble >= 0 ? (uint64_t)member->valuedouble : 0;
}

static inline const char *const *routing_texts_of(const cJSON *array, size_t *count) {
    *count = (size_t)cJSON_GetArraySize(array);
    const char **texts = routing_blocks_take(*count * sizeof *texts);
    size_t index = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, array) {
        texts[index++] = cJSON_GetStringValue(item);
    }
    return texts;
}

/** An object's members as pairs; 0 when a member is not a string, which a pair cannot hold. */
static inline int routing_pairs_of(const cJSON *object, const forebay_pair **pairs, size_t *count) {
    *count = (size_t)cJSON_GetArraySize(object);
    forebay_pair *read = routing_blocks_take(*count * sizeof *read);
    size_t index = 0;
    const cJSON *member = NULL;
    cJSON_ArrayForEach(member, object) {
        if (!cJSON_IsString(member)) {
            return 0;
        }
        read[index].name = member->string;
        read[index++].value = member->valuestring;
    }
    *pairs = read;
    return 1;
}

static inline void routing_assignment_of(const cJSON *json, forebay_assignment *assignment) {
    assignment->provider = routing_text_of(json, "provider");
    assignment->model = routing_text_of(json, "model");
    assignment->name = routing_text_of(json, "name");
    assignment->derived = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "derived")) ? 1u : 0u;
}

static inline const forebay_slot *routing_slots_of(const cJSON *map, size_t *count) {
    *count = (size_t)cJSON_GetArraySize(map);
    forebay_slot *slots = routing_blocks_take(*count * sizeof *slots);
    size_t index = 0;
    const cJSON *member = NULL;
    cJSON_ArrayForEach(member, map) {
        forebay_slot *slot = &slots[index++];
        forebay_assignment *chain = routing_blocks_take((size_t)cJSON_GetArraySize(member) * sizeof *chain);
        size_t entry = 0;
        const cJSON *item = NULL;
        cJSON_ArrayForEach(item, member) {
            routing_assignment_of(item, &chain[entry++]);
        }
        slot->slot = member->string;
        slot->chain = chain;
        slot->count = entry;
    }
    return slots;
}

static inline void routing_catalog_provider_of(const cJSON *entry, forebay_catalog_provider *provider) {
    const cJSON *models = cJSON_GetObjectItemCaseSensitive(entry, "models");
    const cJSON *ranking = cJSON_GetObjectItemCaseSensitive(entry, "ranking");
    forebay_catalog_model *read = routing_blocks_take((size_t)cJSON_GetArraySize(models) * sizeof *read);
    size_t index = 0;
    const cJSON *model = NULL;
    cJSON_ArrayForEach(model, models) {
        const cJSON *context = cJSON_GetObjectItemCaseSensitive(model, "context");
        const cJSON *output = cJSON_GetObjectItemCaseSensitive(model, "output");
        read[index].id = model->string;
        read[index].name = routing_text_of(model, "name");
        read[index].has_context = cJSON_IsNumber(context) ? 1u : 0u;
        read[index].context = routing_count_of(model, "context");
        read[index].has_output = cJSON_IsNumber(output) ? 1u : 0u;
        read[index++].output = routing_count_of(model, "output");
    }
    provider->provider = entry->string;
    provider->models = read;
    provider->model_count = index;
    provider->has_ranking = cJSON_IsArray(ranking) ? 1u : 0u;
    provider->ranking = routing_texts_of(ranking, &provider->ranking_count);
    provider->default_model = routing_text_of(entry, "defaultModel");
}

static inline const forebay_catalog_provider *routing_catalog_of(const cJSON *models, size_t *count) {
    *count = (size_t)cJSON_GetArraySize(models);
    forebay_catalog_provider *providers = routing_blocks_take(*count * sizeof *providers);
    size_t index = 0;
    const cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, models) {
        routing_catalog_provider_of(entry, &providers[index++]);
    }
    return providers;
}

static inline const forebay_routing_profile *routing_profile_of(const cJSON *json) {
    forebay_routing_profile *profile = routing_blocks_take(sizeof *profile);
    profile->size = (uint32_t)sizeof *profile;
    profile->tier_source_provider = routing_text_of(json, "tierSourceProvider");
    profile->tier_order = routing_texts_of(cJSON_GetObjectItemCaseSensitive(json, "tierOrder"),
                                           &profile->tier_order_count);
    profile->tier_fallback = routing_texts_of(cJSON_GetObjectItemCaseSensitive(json, "tierFallback"),
                                              &profile->tier_fallback_count);
    profile->tier_regex = routing_text_of(json, "tierRegex");
    profile->native_model_pattern = routing_text_of(json, "nativeModelPattern");
    profile->unserved_model_pattern = routing_text_of(json, "unservedModelPattern");
    profile->default_context = routing_count_of(json, "defaultContext");
    profile->default_output = routing_count_of(json, "defaultOutput");
    return profile;
}

/** A JSON object of pairs, members in order. */
static inline cJSON *routing_pairs_json_of(const forebay_pair *pairs, size_t count) {
    cJSON *object = cJSON_CreateObject();
    for (size_t index = 0; index < count; index++) {
        cJSON_AddStringToObject(object, pairs[index].name, pairs[index].value);
    }
    return object;
}

static inline cJSON *routing_texts_json_of(const char *const *texts, size_t count) {
    cJSON *array = cJSON_CreateArray();
    for (size_t index = 0; index < count; index++) {
        cJSON_AddItemToArray(array, cJSON_CreateString(texts[index]));
    }
    return array;
}

static inline cJSON *routing_chain_json_of(const forebay_assignment *chain, size_t count) {
    cJSON *array = cJSON_CreateArray();
    for (size_t index = 0; index < count; index++) {
        cJSON *assignment = cJSON_CreateObject();
        cJSON_AddStringToObject(assignment, "provider", chain[index].provider);
        cJSON_AddStringToObject(assignment, "model", chain[index].model);
        cJSON_AddStringToObject(assignment, "name", chain[index].name);
        cJSON_AddBoolToObject(assignment, "derived", chain[index].derived);
        cJSON_AddItemToArray(array, assignment);
    }
    return array;
}

static inline cJSON *routing_log_json_of(const forebay_log_line *log, size_t count) {
    cJSON *array = cJSON_CreateArray();
    for (size_t index = 0; index < count; index++) {
        cJSON *line = cJSON_CreateObject();
        cJSON_AddStringToObject(line, "level", log[index].level == FOREBAY_LOG_WARN ? "warn" : "info");
        cJSON_AddStringToObject(line, "message", log[index].message);
        cJSON_AddItemToArray(array, line);
    }
    return array;
}

static inline const char *routing_reply_kind_name(uint32_t kind) {
    static const char *const kinds[] = {"(none)",          "proxyError",  "apiError",
                                        "providerFailure", "rateLimited", "invalidRequest"};
    return kind <= 5 ? kinds[kind] : "(unknown)";
}

#endif
