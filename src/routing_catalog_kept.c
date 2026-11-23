#include <string.h>

#include "answer_storage.h"
#include "routing_arguments.h"
#include "routing_catalog_state.h"
#include "routing_model_map.h"
#include "spisor/allocation.h"

static int copy_model(void **storage, const forebay_catalog_model *from, forebay_catalog_model *to) {
    *to = *from;
    return forebay_storage_text(storage, from->id, &to->id) && forebay_storage_text(storage, from->name, &to->name);
}

int routing_catalog_copy_provider(void **storage, const forebay_catalog_provider *from, forebay_catalog_provider *to) {
    forebay_catalog_model *models = forebay_storage_array(storage, from->model_count, sizeof *models);
    const char **ranking = forebay_storage_array(storage, from->ranking_count, sizeof *ranking);
    if ((from->model_count > 0 && models == NULL) || (from->ranking_count > 0 && ranking == NULL)) {
        return 0;
    }
    *to = *from;
    to->models = models;
    to->ranking = ranking;
    for (size_t index = 0; index < from->model_count; index++) {
        if (!copy_model(storage, &from->models[index], &models[index])) {
            return 0;
        }
    }
    for (size_t index = 0; index < from->ranking_count; index++) {
        if (!forebay_storage_text(storage, from->ranking[index], &ranking[index])) {
            return 0;
        }
    }
    return forebay_storage_text(storage, from->provider, &to->provider) &&
           forebay_storage_text(storage, from->default_model, &to->default_model);
}

catalog_round *routing_catalog_new_round(uint64_t id, double started, const char *const *implementations,
                                         size_t count) {
    void *storage = NULL;
    catalog_round *round = forebay_storage_allocate(&storage, sizeof *round);
    const char **names = forebay_storage_array(&storage, count, sizeof *names);
    unsigned char *settled = forebay_storage_array(&storage, count, 1);
    if (round == NULL || (count > 0 && (names == NULL || settled == NULL))) {
        forebay_storage_release(storage);
        return NULL;
    }
    for (size_t index = 0; index < count; index++) {
        if (!forebay_storage_text(&storage, implementations[index], &names[index])) {
            forebay_storage_release(storage);
            return NULL;
        }
    }
    round->id = id;
    round->started = started;
    round->implementations = names;
    round->settled = settled;
    round->count = count;
    round->storage = storage;
    return round;
}

void routing_catalog_free_round(catalog_round *round) {
    if (round != NULL) {
        forebay_storage_release(round->storage);
    }
}

catalog_kept *routing_catalog_new_kept(const char *implementation, const forebay_catalog_provider *provider) {
    void *storage = NULL;
    catalog_kept *kept = forebay_storage_allocate(&storage, sizeof *kept);
    if (kept == NULL || !forebay_storage_text(&storage, implementation, &kept->implementation) ||
        !routing_catalog_copy_provider(&storage, provider, &kept->provider)) {
        forebay_storage_release(storage);
        return NULL;
    }
    kept->storage = storage;
    return kept;
}

void routing_catalog_free_kept(catalog_kept *kept) {
    if (kept != NULL) {
        forebay_storage_release(kept->storage);
    }
}

const catalog_kept *routing_catalog_kept_of(const forebay_catalog *catalog, const catalog_kept *fresh,
                                            const char *implementation) {
    if (fresh != NULL && strcmp(fresh->implementation, implementation) == 0) {
        return fresh;
    }
    for (const catalog_kept *kept = catalog->kept; kept != NULL; kept = kept->next) {
        if (strcmp(kept->implementation, implementation) == 0) {
            return kept;
        }
    }
    return NULL;
}

/** The providers `round` reads, in its order, borrowed from the kept answers; NULL when memory runs out. */
static forebay_catalog_provider *providers_of(const forebay_catalog *catalog, const catalog_round *round,
                                              const catalog_kept *fresh, size_t *count) {
    forebay_catalog_provider *providers = spisor_memory_allocate_zeroed(round->count + 1, sizeof *providers);
    *count = 0;
    for (size_t index = 0; providers != NULL && index < round->count; index++) {
        const catalog_kept *kept = routing_catalog_kept_of(catalog, fresh, round->implementations[index]);
        if (kept != NULL) {
            providers[(*count)++] = kept->provider;
        }
    }
    return providers;
}

static cJSON *derivation_call(const forebay_catalog *catalog, const forebay_catalog_provider *providers,
                              size_t count) {
    cJSON *call = cJSON_CreateObject();
    if (call == NULL || !routing_add_json(call, "profile", cJSON_CreateObjectReference(catalog->profile->child)) ||
        !routing_add_json(call, "settings", cJSON_CreateObjectReference(catalog->settings->child)) ||
        !routing_add_json(call, "models", routing_catalog_json(providers, count))) {
        cJSON_Delete(call);
        return NULL;
    }
    return call;
}

static const char NO_MAP_WARNING[] = "the model map could not be derived, so routing keeps the last one: ";

int routing_catalog_warn(cJSON *log, const char *prefix, const char *text) {
    size_t prefix_length = strlen(prefix);
    size_t text_length = strlen(text);
    char *message = spisor_memory_allocate(prefix_length + text_length + 1);
    cJSON *line = cJSON_CreateObject();
    int added = message != NULL && line != NULL;
    if (added) {
        memcpy(message, prefix, prefix_length);
        memcpy(message + prefix_length, text, text_length + 1);
        added = cJSON_AddStringToObject(line, "level", "warn") != NULL &&
                cJSON_AddStringToObject(line, "message", message) != NULL && cJSON_AddItemToArray(log, line);
    }
    spisor_memory_release(message);
    if (!added) {
        cJSON_Delete(line);
    }
    return added;
}

static spisor_status derived_map(const service_reply *reply, spisor_status status, cJSON **map, cJSON *log) {
    if (status == SPISOR_OK) {
        *map = cJSON_DetachItemFromObjectCaseSensitive(reply->result, "map");
        return *map == NULL ? SPISOR_OUT_OF_MEMORY : SPISOR_OK;
    }
    if (status == SPISOR_REFUSED || status == SPISOR_MALFORMED) {
        const char *detail = reply->refusal->detail == NULL ? "" : reply->refusal->detail;
        return routing_catalog_warn(log, NO_MAP_WARNING, detail) ? SPISOR_OK : SPISOR_OUT_OF_MEMORY;
    }
    return SPISOR_OUT_OF_MEMORY;
}

spisor_status routing_catalog_derive(const forebay_catalog *catalog, const catalog_round *round,
                                     const catalog_kept *fresh, cJSON **map, cJSON *log) {
    size_t count = 0;
    forebay_catalog_provider *providers = providers_of(catalog, round, fresh, &count);
    cJSON *call = providers == NULL ? NULL : derivation_call(catalog, providers, count);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    service_reply reply = {SPISOR_OK, NULL, &refusal};
    spisor_status status = SPISOR_OUT_OF_MEMORY;
    *map = NULL;
    if (call != NULL) {
        status = routing_model_map_call(call, &reply);
        status = derived_map(&reply, status, map, log);
    }
    cJSON_Delete(reply.result);
    cJSON_Delete(call);
    spisor_memory_release(providers);
    spisor_refusal_release(&refusal);
    return status;
}
