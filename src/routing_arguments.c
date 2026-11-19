#include "routing_arguments.h"

#include "definition_support.h"

#define PROFILE_SHAPE FOREBAY_SHAPE_THROUGH(forebay_routing_profile, unserved_model_pattern)

int routing_are_texts(const char *const *texts, size_t count) {
    if (!ROUTING_IS_ARRAY(texts, count)) {
        return 0;
    }
    for (size_t index = 0; index < count; index++) {
        if (texts[index] == NULL) {
            return 0;
        }
    }
    return 1;
}

int routing_are_pairs(const forebay_pair *pairs, size_t count) {
    if (!ROUTING_IS_ARRAY(pairs, count)) {
        return 0;
    }
    for (size_t index = 0; index < count; index++) {
        if (pairs[index].name == NULL || pairs[index].value == NULL) {
            return 0;
        }
    }
    return 1;
}

static int is_assignment(const forebay_assignment *assignment) {
    return assignment->provider != NULL && assignment->model != NULL;
}

int routing_are_slots(const forebay_slot *slots, size_t count) {
    if (!ROUTING_IS_ARRAY(slots, count)) {
        return 0;
    }
    for (size_t index = 0; index < count; index++) {
        if (slots[index].slot == NULL || !ROUTING_IS_ARRAY(slots[index].chain, slots[index].count)) {
            return 0;
        }
        for (size_t entry = 0; entry < slots[index].count; entry++) {
            if (!is_assignment(&slots[index].chain[entry])) {
                return 0;
            }
        }
    }
    return 1;
}

static int is_catalog_provider(const forebay_catalog_provider *provider) {
    if (provider->provider == NULL || !ROUTING_IS_ARRAY(provider->models, provider->model_count) ||
        !routing_are_texts(provider->ranking, provider->ranking_count)) {
        return 0;
    }
    for (size_t index = 0; index < provider->model_count; index++) {
        if (provider->models[index].id == NULL) {
            return 0;
        }
    }
    return 1;
}

int routing_is_catalog(const forebay_catalog_provider *providers, size_t count) {
    if (!ROUTING_IS_ARRAY(providers, count)) {
        return 0;
    }
    for (size_t index = 0; index < count; index++) {
        if (!is_catalog_provider(&providers[index])) {
            return 0;
        }
    }
    return 1;
}

int routing_is_profile(const forebay_routing_profile *profile, int needs_tiers) {
    if (!FOREBAY_HAS_SHAPE(profile, PROFILE_SHAPE) ||
        !routing_are_texts(profile->tier_order, profile->tier_order_count) ||
        !routing_are_texts(profile->tier_fallback, profile->tier_fallback_count)) {
        return 0;
    }
    return !needs_tiers || (profile->tier_source_provider != NULL && profile->tier_regex != NULL);
}

int routing_add_json(cJSON *object, const char *name, cJSON *item) {
    if (item == NULL) {
        return 0;
    }
    if (!cJSON_AddItemToObject(object, name, item)) {
        cJSON_Delete(item);
        return 0;
    }
    return 1;
}

static int push_json(cJSON *array, cJSON *item) {
    if (item == NULL) {
        return 0;
    }
    if (!cJSON_AddItemToArray(array, item)) {
        cJSON_Delete(item);
        return 0;
    }
    return 1;
}

/** Sets `name` to `item`, replacing a member already there, as a later pair replaces an earlier one. */
static int set_json(cJSON *object, const char *name, cJSON *item) {
    if (item == NULL) {
        return 0;
    }
    if (cJSON_GetObjectItemCaseSensitive(object, name) == NULL) {
        return routing_add_json(object, name, item);
    }
    if (!cJSON_ReplaceItemInObjectCaseSensitive(object, name, item)) {
        cJSON_Delete(item);
        return 0;
    }
    return 1;
}

cJSON *routing_pairs_json(const forebay_pair *pairs, size_t count) {
    cJSON *object = cJSON_CreateObject();
    for (size_t index = 0; object != NULL && index < count; index++) {
        if (!set_json(object, pairs[index].name, cJSON_CreateString(pairs[index].value))) {
            cJSON_Delete(object);
            return NULL;
        }
    }
    return object;
}

cJSON *routing_texts_json(const char *const *texts, size_t count) {
    cJSON *array = cJSON_CreateArray();
    for (size_t index = 0; array != NULL && index < count; index++) {
        if (!push_json(array, cJSON_CreateString(texts[index]))) {
            cJSON_Delete(array);
            return NULL;
        }
    }
    return array;
}

static cJSON *assignment_json(const forebay_assignment *assignment) {
    cJSON *object = cJSON_CreateObject();
    if (object == NULL || cJSON_AddStringToObject(object, "provider", assignment->provider) == NULL ||
        cJSON_AddStringToObject(object, "model", assignment->model) == NULL ||
        (assignment->name != NULL && cJSON_AddStringToObject(object, "name", assignment->name) == NULL) ||
        cJSON_AddBoolToObject(object, "derived", assignment->derived != 0) == NULL) {
        cJSON_Delete(object);
        return NULL;
    }
    return object;
}

static cJSON *chain_json(const forebay_slot *slot) {
    cJSON *chain = cJSON_CreateArray();
    for (size_t index = 0; chain != NULL && index < slot->count; index++) {
        if (!push_json(chain, assignment_json(&slot->chain[index]))) {
            cJSON_Delete(chain);
            return NULL;
        }
    }
    return chain;
}

cJSON *routing_slots_json(const forebay_slot *slots, size_t count) {
    cJSON *map = cJSON_CreateObject();
    for (size_t index = 0; map != NULL && index < count; index++) {
        if (!set_json(map, slots[index].slot, chain_json(&slots[index]))) {
            cJSON_Delete(map);
            return NULL;
        }
    }
    return map;
}

static cJSON *model_json(const forebay_catalog_model *model) {
    cJSON *object = cJSON_CreateObject();
    if (object != NULL && model->name != NULL && cJSON_AddStringToObject(object, "name", model->name) == NULL) {
        cJSON_Delete(object);
        return NULL;
    }
    return object;
}

static cJSON *models_json(const forebay_catalog_provider *provider) {
    cJSON *models = cJSON_CreateObject();
    for (size_t index = 0; models != NULL && index < provider->model_count; index++) {
        if (!set_json(models, provider->models[index].id, model_json(&provider->models[index]))) {
            cJSON_Delete(models);
            return NULL;
        }
    }
    return models;
}

static cJSON *catalog_provider_json(const forebay_catalog_provider *provider) {
    cJSON *entry = cJSON_CreateObject();
    if (entry == NULL || !routing_add_json(entry, "models", models_json(provider)) ||
        (provider->has_ranking &&
         !routing_add_json(entry, "ranking", routing_texts_json(provider->ranking, provider->ranking_count)))) {
        cJSON_Delete(entry);
        return NULL;
    }
    return entry;
}

cJSON *routing_catalog_json(const forebay_catalog_provider *providers, size_t count) {
    cJSON *catalog = cJSON_CreateObject();
    for (size_t index = 0; catalog != NULL && index < count; index++) {
        if (!set_json(catalog, providers[index].provider, catalog_provider_json(&providers[index]))) {
            cJSON_Delete(catalog);
            return NULL;
        }
    }
    return catalog;
}

static int add_text(cJSON *object, const char *name, const char *text) {
    return text == NULL || cJSON_AddStringToObject(object, name, text) != NULL;
}

cJSON *routing_profile_json(const forebay_routing_profile *profile) {
    cJSON *object = cJSON_CreateObject();
    if (object == NULL || !add_text(object, "tierSourceProvider", profile->tier_source_provider) ||
        !routing_add_json(object, "tierOrder", routing_texts_json(profile->tier_order, profile->tier_order_count)) ||
        !routing_add_json(object, "tierFallback",
                          routing_texts_json(profile->tier_fallback, profile->tier_fallback_count)) ||
        !add_text(object, "tierRegex", profile->tier_regex) ||
        !add_text(object, "nativeModelPattern", profile->native_model_pattern) ||
        !add_text(object, "unservedModelPattern", profile->unserved_model_pattern)) {
        cJSON_Delete(object);
        return NULL;
    }
    return object;
}
