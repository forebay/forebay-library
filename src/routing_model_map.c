#include "routing_model_map.h"

#include <string.h>

#include "routing_tiers.h"

#define DEFAULT_SLOT "default"

static const char *text_of(const cJSON *entry, const char *member) {
    return cJSON_GetObjectItemCaseSensitive(entry, member)->valuestring;
}

static const cJSON *find_entry(const cJSON *catalog, const char *provider, const char *model) {
    const cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, catalog) {
        if (strcmp(text_of(entry, "provider"), provider) == 0 && strcmp(text_of(entry, "model"), model) == 0) {
            return entry;
        }
    }
    return NULL;
}

static int provider_known(const cJSON *catalog, const char *provider) {
    const cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, catalog) {
        if (strcmp(text_of(entry, "provider"), provider) == 0) {
            return 1;
        }
    }
    return 0;
}

/** The first catalog entry of the provider (any provider when NULL) whose id holds the keyword. */
static const cJSON *derive_in(const cJSON *catalog, const char *provider, const char *keyword) {
    if (keyword == NULL || keyword[0] == '\0') {
        return NULL;
    }
    const cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, catalog) {
        if ((provider == NULL || strcmp(text_of(entry, "provider"), provider) == 0) &&
            service_contains_ignoring_case(text_of(entry, "model"), keyword)) {
            return entry;
        }
    }
    return NULL;
}

static cJSON *assignment_from_entry(const cJSON *entry, int derived) {
    return routing_assignment(text_of(entry, "provider"), text_of(entry, "model"), text_of(entry, "name"), derived);
}

static int keep_stored(cJSON *kept, const cJSON *catalog, const cJSON *stored_entry) {
    const char *provider = text_of(stored_entry, "provider");
    const char *model = text_of(stored_entry, "model");
    const cJSON *found = find_entry(catalog, provider, model);
    if (found != NULL) {
        return service_push(kept, assignment_from_entry(found, 0));
    }
    if (!provider_known(catalog, provider)) {
        return service_push(kept, routing_assignment(provider, model, model, 0));
    }
    return 1;
}

static int derive_into(cJSON *chain, const cJSON *entry) {
    return entry == NULL || service_push(chain, assignment_from_entry(entry, 1));
}

static int derive_unset(cJSON *chain, const cJSON *catalog, const routing_profile *profile, const char *keyword) {
    const cJSON *entry = derive_in(catalog, profile->tier_source_provider, keyword);
    if (entry == NULL) {
        entry = derive_in(catalog, NULL, keyword);
    }
    return derive_into(chain, entry);
}

/**
 * A stored entry is kept while the catalog holds its model; a fully stale chain heals only inside
 * the provider it named, and only a slot with nothing stored derives from the whole catalog.
 */
static cJSON *pick(const cJSON *catalog, const cJSON *stored_map, const routing_profile *profile, const char *slot,
                   const char *keyword) {
    cJSON *stored_chain = routing_normalize_chain(cJSON_GetObjectItemCaseSensitive(stored_map, slot));
    cJSON *picked = cJSON_CreateArray();
    int ok = stored_chain != NULL && picked != NULL;
    const cJSON *stored_entry = NULL;
    cJSON_ArrayForEach(stored_entry, stored_chain) {
        ok = ok && keep_stored(picked, catalog, stored_entry);
    }
    if (ok && cJSON_GetArraySize(picked) == 0) {
        if (cJSON_GetArraySize(stored_chain) > 0) {
            ok = derive_into(picked, derive_in(catalog, text_of(stored_chain->child, "provider"), keyword));
        } else {
            ok = derive_unset(picked, catalog, profile, keyword);
        }
    }
    cJSON_Delete(stored_chain);
    if (!ok) {
        cJSON_Delete(picked);
        return NULL;
    }
    return picked;
}

static int set_slot(cJSON *map, const char *slot, cJSON *chain) {
    if (chain == NULL) {
        return 0;
    }
    if (cJSON_GetObjectItemCaseSensitive(map, slot) != NULL) {
        return cJSON_ReplaceItemInObjectCaseSensitive(map, slot, chain);
    }
    return service_add(map, slot, chain);
}

static const cJSON *first_filled_slot(const cJSON *map, const cJSON *tiers) {
    const cJSON *tier = NULL;
    cJSON_ArrayForEach(tier, tiers) {
        const cJSON *chain = cJSON_GetObjectItemCaseSensitive(map, tier->valuestring);
        if (cJSON_GetArraySize(chain) > 0) {
            return chain;
        }
    }
    return NULL;
}

static cJSON *derived_copy(const cJSON *chain) {
    cJSON *copy = cJSON_CreateArray();
    if (copy == NULL) {
        return NULL;
    }
    const cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, chain) {
        if (!service_push(copy, routing_assignment(text_of(entry, "provider"), text_of(entry, "model"),
                                                   text_of(entry, "name"), 1))) {
            cJSON_Delete(copy);
            return NULL;
        }
    }
    return copy;
}

static cJSON *default_chain(const cJSON *map, const cJSON *tiers, const cJSON *catalog, const cJSON *stored_map,
                            const routing_profile *profile) {
    cJSON *own = pick(catalog, stored_map, profile, DEFAULT_SLOT, NULL);
    if (own == NULL || cJSON_GetArraySize(own) > 0) {
        return own;
    }
    cJSON_Delete(own);
    const cJSON *first = first_filled_slot(map, tiers);
    return first == NULL ? cJSON_CreateArray() : derived_copy(first);
}

static cJSON *resolve_map(const cJSON *tiers, const cJSON *catalog, const cJSON *stored_map,
                          const routing_profile *profile) {
    cJSON *map = cJSON_CreateObject();
    int ok = map != NULL;
    const cJSON *tier = NULL;
    cJSON_ArrayForEach(tier, tiers) {
        ok = ok && set_slot(map, tier->valuestring,
                            pick(catalog, stored_map, profile, tier->valuestring, tier->valuestring));
    }
    ok = ok && set_slot(map, DEFAULT_SLOT, default_chain(map, tiers, catalog, stored_map, profile));
    if (!ok) {
        cJSON_Delete(map);
        return NULL;
    }
    return map;
}

/** Slots whose primary is derived, which is what the router announces as a healed mapping. */
static cJSON *healed_slots(const cJSON *map) {
    cJSON *healed = cJSON_CreateArray();
    if (healed == NULL) {
        return NULL;
    }
    const cJSON *chain = NULL;
    cJSON_ArrayForEach(chain, map) {
        const cJSON *primary = chain->child;
        if (primary != NULL && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(primary, "derived")) &&
            !service_push(healed, cJSON_CreateString(chain->string))) {
            cJSON_Delete(healed);
            return NULL;
        }
    }
    return healed;
}

static cJSON *answer_for(const cJSON *tiers, const cJSON *catalog, const cJSON *stored_map,
                         const routing_profile *profile) {
    cJSON *map = resolve_map(tiers, catalog, stored_map, profile);
    cJSON *healed = map == NULL ? NULL : healed_slots(map);
    cJSON *result = cJSON_CreateObject();
    if (healed == NULL || result == NULL || !service_add(result, "map", map)) {
        cJSON_Delete(healed);
        cJSON_Delete(result);
        return NULL;
    }
    if (!service_add(result, "healed", healed)) {
        cJSON_Delete(result);
        return NULL;
    }
    return result;
}

/** The plugin's stored map, `settings.routing.modelMap`, or the empty map when any step of that path is absent. */
static const cJSON *stored_map_of(const cJSON *settings, const cJSON *empty) {
    const cJSON *routing = cJSON_GetObjectItemCaseSensitive(settings, "routing");
    const cJSON *declared = cJSON_GetObjectItemCaseSensitive(routing, "modelMap");
    return cJSON_IsObject(declared) ? declared : empty;
}

static int32_t answer_call(const cJSON *profile_json, const cJSON *settings, const cJSON *models, uint8_t **out,
                           size_t *out_len) {
    int32_t status = SPISOR_STATUS_SUCCESS;
    routing_profile profile;
    if (!routing_read_profile(profile_json, NEEDS_TIERS | NEEDS_UNSERVED_PATTERN, &profile, out, out_len, &status)) {
        return status;
    }
    routing_refusal refusal = {0, "", NULL};
    cJSON *tiers = routing_detect_tiers(&profile, models, &refusal);
    if (tiers == NULL) {
        return routing_answer_refusal(&refusal, out, out_len);
    }
    routing_pattern *unserved = NULL;
    if (!routing_compile_unserved_pattern(&profile, &unserved, &refusal)) {
        cJSON_Delete(tiers);
        return refusal.refused ? routing_answer_refusal(&refusal, out, out_len)
                               : service_answer(NULL, out, out_len);
    }
    cJSON *catalog = routing_catalog(models, unserved);
    cJSON *empty = cJSON_CreateObject();
    cJSON *result = (catalog == NULL || empty == NULL)
                        ? NULL
                        : answer_for(tiers, catalog, stored_map_of(settings, empty), &profile);
    status = service_answer(result, out, out_len);
    cJSON_Delete(tiers);
    cJSON_Delete(catalog);
    cJSON_Delete(empty);
    routing_pattern_free(unserved);
    return status;
}

SERVICE_EXPORT int32_t forebay_library_routing_model_map(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len) {
    spisor_call call;
    int32_t status = service_open(in, in_len, &call, out, out_len);
    if (status != SPISOR_STATUS_SUCCESS) {
        return status;
    }
    const cJSON *profile = NULL;
    const cJSON *settings = NULL;
    const cJSON *models = NULL;
    if (service_require(&call, "profile", service_is_object, "an object", &profile, out, out_len, &status) &&
        service_require(&call, "settings", service_is_object, "an object", &settings, out, out_len, &status) &&
        service_require(&call, "models", service_is_object, "an object", &models, out, out_len, &status)) {
        status = answer_call(profile, settings, models, out, out_len);
    }
    spisor_discard_call(&call);
    return status;
}
