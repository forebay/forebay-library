#include "forebay/routing.h"

#include <stdlib.h>
#include <string.h>

#include "routing_step.h"
#include "routing_support.h"

#define FORBIDDEN_STATUS 403
#define HEALTH_PATH "/health"
#define LOOPBACK_ADDRESS "127.0.0.1"
#define LOOPBACK_NAME "localhost"
#define HIGHEST_PORT 65535

struct forebay_walk {
    cJSON *walk;
};

/** What a step answer points at: the captured answer tree, and the arrays built over it. */
typedef struct step_storage {
    cJSON *tree;
    forebay_header *headers;
    forebay_log_line *log;
} step_storage;

typedef struct named_kind {
    const char *name;
    uint32_t kind;
} named_kind;

static const named_kind step_kinds[] = {
    {"passThrough", FOREBAY_STEP_PASS_THROUGH}, {"refreshModels", FOREBAY_STEP_REFRESH_MODELS},
    {"callProvider", FOREBAY_STEP_CALL_PROVIDER}, {"reply", FOREBAY_STEP_REPLY},
    {"endStream", FOREBAY_STEP_END_STREAM},     {"done", FOREBAY_STEP_DONE},
};

static const named_kind reply_kinds[] = {
    {"proxyError", FOREBAY_REPLY_PROXY_ERROR},
    {"apiError", FOREBAY_REPLY_API_ERROR},
    {"providerFailure", FOREBAY_REPLY_PROVIDER_FAILURE},
    {"rateLimited", FOREBAY_REPLY_RATE_LIMITED},
};

static const char *report_kind_names[] = {NULL, "response", "streamed", "streamFailed", "thrown", "failure"};

static uint32_t kind_named(const named_kind *kinds, size_t count, const char *name) {
    for (size_t index = 0; name != NULL && index < count; index++) {
        if (strcmp(kinds[index].name, name) == 0) {
            return kinds[index].kind;
        }
    }
    return 0;
}

static const char *string_member(const cJSON *object, const char *name) {
    const cJSON *member = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(member) ? member->valuestring : NULL;
}

static double number_member(const cJSON *object, const char *name) {
    const cJSON *member = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsNumber(member) ? member->valuedouble : 0;
}

static int has_room(uint32_t size, size_t first_shape) {
    return size >= first_shape;
}

static int is_empty_answer(uint32_t size, size_t first_shape, const void *storage) {
    return has_room(size, first_shape) && storage == NULL;
}

static void clear_keeping_size(void *answer, size_t length) {
    uint32_t size = *(uint32_t *)answer;
    memset(answer, 0, length);
    *(uint32_t *)answer = size;
}

static spisor_status refuse_with(spisor_refusal *refusal, const char *detail, const char *fix, spisor_status status) {
    if (refusal == NULL || !has_room(refusal->size, sizeof(spisor_refusal)) || refusal->storage != NULL) {
        return status;
    }
    size_t detail_length = strlen(detail) + 1;
    size_t fix_length = fix == NULL ? 0 : strlen(fix) + 1;
    char *storage = malloc(detail_length + fix_length);
    if (storage == NULL) {
        return SPISOR_OUT_OF_MEMORY;
    }
    memcpy(storage, detail, detail_length);
    if (fix != NULL) {
        memcpy(storage + detail_length, fix, fix_length);
    }
    refusal->detail = storage;
    refusal->fix = fix == NULL ? NULL : storage + detail_length;
    refusal->storage = storage;
    return status;
}

/** The exact name first, then the first matching ignoring case, as routing_header reads a JSON object. */
static const char *typed_header(const forebay_headers *headers, const char *name) {
    for (size_t index = 0; index < headers->count; index++) {
        if (strcmp(headers->entries[index].name, name) == 0) {
            return headers->entries[index].value;
        }
    }
    for (size_t index = 0; index < headers->count; index++) {
        if (routing_equals_ignoring_case(headers->entries[index].name, name)) {
            return headers->entries[index].value;
        }
    }
    return NULL;
}

static int typed_carries_an_origin(const forebay_headers *headers) {
    for (size_t index = 0; index < headers->count; index++) {
        if (routing_equals_ignoring_case(headers->entries[index].name, "origin")) {
            return 1;
        }
    }
    return 0;
}

static int typed_is_addressed_to_the_port(const forebay_headers *headers, uint32_t port) {
    const char *host = typed_header(headers, "host");
    char *address = routing_formatted(LOOPBACK_ADDRESS ":%u", (unsigned)port);
    char *name = routing_formatted(LOOPBACK_NAME ":%u", (unsigned)port);
    int admitted = host != NULL && address != NULL && name != NULL &&
                   (routing_equals_ignoring_case(host, address) || routing_equals_ignoring_case(host, name));
    free(address);
    free(name);
    return admitted;
}

static int typed_is_health_probe(const char *path) {
    size_t length = strcspn(path, "?");
    return length == strlen(HEALTH_PATH) && strncmp(path, HEALTH_PATH, length) == 0;
}

static int are_headers_whole(const forebay_headers *headers) {
    if (headers->count > 0 && headers->entries == NULL) {
        return 0;
    }
    for (size_t index = 0; index < headers->count; index++) {
        if (headers->entries[index].name == NULL || headers->entries[index].value == NULL) {
            return 0;
        }
    }
    return 1;
}

static spisor_status answer_refused_arrival(uint32_t port, forebay_arrival_answer *answer) {
    char *message = routing_formatted("the proxy answers only requests to " LOOPBACK_ADDRESS ":%u or " LOOPBACK_NAME
                                      ":%u that carry no Origin",
                                      (unsigned)port, (unsigned)port);
    if (message == NULL) {
        return SPISOR_OUT_OF_MEMORY;
    }
    answer->kind = FOREBAY_ARRIVAL_REFUSE;
    answer->reply_kind = FOREBAY_REPLY_PROXY_ERROR;
    answer->status = FORBIDDEN_STATUS;
    answer->message = message;
    answer->storage = message;
    return SPISOR_OK;
}

static spisor_status arrival(void *context, const forebay_arrival_argument *argument, forebay_arrival_answer *answer,
                             spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !has_room(argument->size, sizeof(*argument)) || answer == NULL ||
        !is_empty_answer(answer->size, sizeof(*answer), answer->storage) || argument->path == NULL ||
        !are_headers_whole(&argument->headers) || argument->port < 1 || argument->port > HIGHEST_PORT) {
        return refuse_with(refusal, "the arrival is not a path, whole headers and a port from 1 to 65535",
                           "send path, headers and port as forebay_arrival_argument documents", SPISOR_MALFORMED);
    }
    clear_keeping_size(answer, sizeof(*answer));
    if (typed_carries_an_origin(&argument->headers) ||
        !typed_is_addressed_to_the_port(&argument->headers, argument->port)) {
        return answer_refused_arrival(argument->port, answer);
    }
    if (typed_is_health_probe(argument->path)) {
        answer->kind = FOREBAY_ARRIVAL_HEALTH;
        answer->status = 200;
        answer->content_type = "text/plain";
        answer->body = "ok";
        return SPISOR_OK;
    }
    answer->kind = FOREBAY_ARRIVAL_ROUTE;
    return SPISOR_OK;
}

static void release_arrival(void *context, forebay_arrival_answer *answer) {
    (void)context;
    if (answer == NULL || !has_room(answer->size, sizeof(*answer))) {
        return;
    }
    free(answer->storage);
    clear_keeping_size(answer, sizeof(*answer));
}

static int add_tree(cJSON *object, const char *name, cJSON *item) {
    if (item == NULL) {
        return 0;
    }
    if (!cJSON_AddItemToObjectCS(object, name, item)) {
        cJSON_Delete(item);
        return 0;
    }
    return 1;
}

static int add_text(cJSON *object, const char *name, const char *text) {
    return add_tree(object, name, text == NULL ? cJSON_CreateNull() : cJSON_CreateString(text));
}

static cJSON *texts_tree(const forebay_texts *texts) {
    cJSON *array = cJSON_CreateArray();
    for (size_t index = 0; array != NULL && index < texts->count; index++) {
        if (!service_push(array, cJSON_CreateString(texts->items[index]))) {
            cJSON_Delete(array);
            return NULL;
        }
    }
    return array;
}

static cJSON *profile_tree(const forebay_profile *profile) {
    cJSON *tree = cJSON_CreateObject();
    if (tree == NULL || !add_text(tree, "tierSourceProvider", profile->tier_source_provider) ||
        !add_tree(tree, "tierOrder", texts_tree(&profile->tier_order)) ||
        !add_tree(tree, "tierFallback", texts_tree(&profile->tier_fallback)) ||
        !add_text(tree, "tierRegex", profile->tier_regex) ||
        !add_text(tree, "nativeModelPattern", profile->native_model_pattern) ||
        (profile->unserved_model_pattern != NULL &&
         !add_text(tree, "unservedModelPattern", profile->unserved_model_pattern))) {
        cJSON_Delete(tree);
        return NULL;
    }
    return tree;
}

static cJSON *assignment_tree(const forebay_assignment *assignment) {
    cJSON *tree = cJSON_CreateObject();
    if (tree == NULL || !add_text(tree, "provider", assignment->provider) ||
        !add_text(tree, "model", assignment->model) || !add_text(tree, "name", assignment->name) ||
        !add_tree(tree, "derived", cJSON_CreateBool(assignment->derived != 0))) {
        cJSON_Delete(tree);
        return NULL;
    }
    return tree;
}

static cJSON *map_tree(const forebay_model_map *map) {
    cJSON *tree = cJSON_CreateObject();
    for (size_t slot = 0; tree != NULL && slot < map->count; slot++) {
        cJSON *chain = cJSON_CreateArray();
        for (size_t entry = 0; chain != NULL && entry < map->slots[slot].count; entry++) {
            if (!service_push(chain, assignment_tree(&map->slots[slot].chain[entry]))) {
                cJSON_Delete(chain);
                chain = NULL;
            }
        }
        if (chain == NULL || !cJSON_AddItemToObject(tree, map->slots[slot].slot, chain)) {
            cJSON_Delete(chain);
            cJSON_Delete(tree);
            return NULL;
        }
    }
    return tree;
}

static cJSON *catalog_provider_tree(const forebay_catalog_provider *provider) {
    cJSON *tree = cJSON_CreateObject();
    cJSON *models = cJSON_CreateObject();
    if (tree == NULL || !add_tree(tree, "models", models) ||
        !add_tree(tree, "ranking", texts_tree(&provider->ranking))) {
        cJSON_Delete(tree);
        return NULL;
    }
    for (size_t index = 0; index < provider->model_count; index++) {
        cJSON *model = cJSON_CreateObject();
        if (model == NULL || (provider->models[index].name != NULL && !add_text(model, "name", provider->models[index].name)) ||
            !cJSON_AddItemToObject(models, provider->models[index].id, model)) {
            cJSON_Delete(model);
            cJSON_Delete(tree);
            return NULL;
        }
    }
    return tree;
}

static cJSON *catalog_tree(const forebay_catalog *catalog) {
    cJSON *tree = cJSON_CreateObject();
    for (size_t index = 0; tree != NULL && index < catalog->count; index++) {
        cJSON *provider = catalog_provider_tree(&catalog->providers[index]);
        if (provider == NULL || !cJSON_AddItemToObject(tree, catalog->providers[index].provider, provider)) {
            cJSON_Delete(provider);
            cJSON_Delete(tree);
            return NULL;
        }
    }
    return tree;
}

static cJSON *headers_tree(const forebay_headers *headers) {
    cJSON *tree = cJSON_CreateObject();
    for (size_t index = 0; tree != NULL && index < headers->count; index++) {
        if (cJSON_AddStringToObject(tree, headers->entries[index].name, headers->entries[index].value) == NULL) {
            cJSON_Delete(tree);
            return NULL;
        }
    }
    return tree;
}

static cJSON *report_tree(const forebay_report *report) {
    cJSON *tree = cJSON_CreateObject();
    if (tree == NULL || !add_text(tree, "kind", report_kind_names[report->kind]) ||
        (report->message != NULL && !add_text(tree, "message", report->message)) ||
        !add_tree(tree, "afterBytes", cJSON_CreateBool(report->after_bytes != 0)) ||
        !add_tree(tree, "status", cJSON_CreateNumber(report->status)) ||
        !add_tree(tree, "headers", headers_tree(&report->headers)) ||
        !add_tree(tree, "now", cJSON_CreateNumber(report->now))) {
        cJSON_Delete(tree);
        return NULL;
    }
    return tree;
}

static int add_first_step_members(cJSON *tree, const forebay_step_argument *argument) {
    cJSON *request = cJSON_CreateObject();
    if (!add_tree(tree, "request", request)) {
        return 0;
    }
    cJSON *wording = cJSON_CreateObject();
    return add_tree(tree, "wording", wording) && add_tree(tree, "profile", profile_tree(argument->profile)) &&
           add_tree(tree, "map", map_tree(&argument->map)) &&
           add_tree(tree, "models", catalog_tree(&argument->models)) &&
           add_tree(tree, "refreshed", cJSON_CreateBool(argument->refreshed != 0)) &&
           add_text(wording, "loginCommand", argument->login_command) &&
           add_text(wording, "settingsFile", argument->settings_file) &&
           (argument->model == NULL || add_text(request, "model", argument->model)) &&
           (!argument->pass_through || add_tree(request, "passThrough", cJSON_CreateTrue()));
}

static cJSON *step_argument_tree(const forebay_step_argument *argument) {
    cJSON *tree = cJSON_CreateObject();
    if (tree == NULL || !add_tree(tree, "providers", texts_tree(&argument->providers))) {
        cJSON_Delete(tree);
        return NULL;
    }
    int added = argument->walk == NULL
                    ? add_first_step_members(tree, argument)
                    : cJSON_AddItemReferenceToObject(tree, "walk", argument->walk->walk) &&
                          add_tree(tree, "report", report_tree(argument->report));
    if (!added) {
        cJSON_Delete(tree);
        return NULL;
    }
    return tree;
}

static int is_step_argument_whole(const forebay_step_argument *argument) {
    if (argument->walk != NULL) {
        return argument->report != NULL && has_room(argument->report->size, sizeof(forebay_report)) &&
               argument->report->kind >= FOREBAY_REPORT_RESPONSE && argument->report->kind <= FOREBAY_REPORT_FAILURE &&
               are_headers_whole(&argument->report->headers);
    }
    return argument->profile != NULL && has_room(argument->profile->size, sizeof(forebay_profile)) &&
           argument->login_command != NULL && argument->settings_file != NULL &&
           (argument->model != NULL || argument->pass_through);
}

static forebay_header *headers_of(const cJSON *object, size_t *count) {
    *count = (size_t)cJSON_GetArraySize(object);
    forebay_header *headers = calloc(*count == 0 ? 1 : *count, sizeof(forebay_header));
    size_t index = 0;
    const cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, object) {
        if (headers != NULL) {
            headers[index].name = entry->string;
            headers[index].value = cJSON_IsString(entry) ? entry->valuestring : "";
            index++;
        }
    }
    return headers;
}

static int read_reply(const cJSON *reply, forebay_step_answer *answer, step_storage *storage) {
    const cJSON *headers = cJSON_GetObjectItemCaseSensitive(reply, "headers");
    answer->reply_kind = kind_named(reply_kinds, sizeof(reply_kinds) / sizeof(reply_kinds[0]), string_member(reply, "kind"));
    answer->status = number_member(reply, "status");
    answer->message = string_member(reply, "message");
    answer->reset_at = number_member(reply, "resetAt");
    if (headers == NULL) {
        return 1;
    }
    storage->headers = headers_of(headers, &answer->headers.count);
    answer->headers.entries = storage->headers;
    return storage->headers != NULL;
}

static int read_log(const cJSON *log, forebay_step_answer *answer, step_storage *storage) {
    size_t count = (size_t)cJSON_GetArraySize(log);
    storage->log = calloc(count == 0 ? 1 : count, sizeof(forebay_log_line));
    if (storage->log == NULL) {
        return 0;
    }
    size_t index = 0;
    const cJSON *line = NULL;
    cJSON_ArrayForEach(line, log) {
        const char *level = string_member(line, "level");
        storage->log[index].level = level != NULL && strcmp(level, "warn") == 0 ? FOREBAY_LOG_WARN : FOREBAY_LOG_INFO;
        storage->log[index].message = string_member(line, "message");
        index++;
    }
    answer->log = storage->log;
    answer->log_count = count;
    return 1;
}

static int read_step(const cJSON *step, forebay_step_answer *answer, step_storage *storage) {
    answer->kind = kind_named(step_kinds, sizeof(step_kinds) / sizeof(step_kinds[0]), string_member(step, "kind"));
    if (answer->kind == FOREBAY_STEP_CALL_PROVIDER) {
        answer->provider = string_member(step, "provider");
        answer->model = string_member(step, "model");
    } else if (answer->kind == FOREBAY_STEP_END_STREAM) {
        const cJSON *event = cJSON_GetObjectItemCaseSensitive(step, "event");
        answer->message = string_member(event, "message");
        answer->error_type = string_member(event, "errorType");
    } else if (answer->kind == FOREBAY_STEP_REPLY) {
        return read_reply(cJSON_GetObjectItemCaseSensitive(step, "reply"), answer, storage);
    }
    return 1;
}

static void free_step_storage(step_storage *storage) {
    if (storage == NULL) {
        return;
    }
    cJSON_Delete(storage->tree);
    free(storage->headers);
    free(storage->log);
    free(storage);
}

static spisor_status take_walk(cJSON *tree, forebay_step_answer *answer) {
    cJSON *walk = cJSON_DetachItemFromObjectCaseSensitive(tree, "walk");
    if (walk == NULL) {
        return SPISOR_OK;
    }
    forebay_walk *handle = malloc(sizeof(*handle));
    if (handle == NULL) {
        cJSON_Delete(walk);
        return SPISOR_OUT_OF_MEMORY;
    }
    handle->walk = walk;
    answer->walk = handle;
    return SPISOR_OK;
}

static spisor_status answer_from_tree(cJSON *tree, forebay_step_answer *answer, spisor_refusal *refusal) {
    const char *refused = string_member(tree, "refused");
    if (refused != NULL) {
        spisor_status status = refuse_with(refusal, refused, string_member(tree, "fix"), SPISOR_REFUSED);
        cJSON_Delete(tree);
        return status;
    }
    step_storage *storage = calloc(1, sizeof(*storage));
    if (storage == NULL) {
        cJSON_Delete(tree);
        return SPISOR_OUT_OF_MEMORY;
    }
    storage->tree = tree;
    if (take_walk(tree, answer) != SPISOR_OK ||
        !read_step(cJSON_GetObjectItemCaseSensitive(tree, "step"), answer, storage) ||
        !read_log(cJSON_GetObjectItemCaseSensitive(tree, "log"), answer, storage)) {
        free_step_storage(storage);
        if (answer->walk != NULL) {
            cJSON_Delete(answer->walk->walk);
            free(answer->walk);
        }
        clear_keeping_size(answer, sizeof(*answer));
        return SPISOR_OUT_OF_MEMORY;
    }
    answer->storage = storage;
    return SPISOR_OK;
}

static spisor_status failure_from_text(int32_t status, uint8_t *out, size_t out_len, spisor_refusal *refusal) {
    if (status == SPISOR_STATUS_OUT_OF_MEMORY || out == NULL) {
        spisor_release(out);
        return SPISOR_OUT_OF_MEMORY;
    }
    cJSON *failure = spisor_parse_json(out, out_len);
    spisor_release(out);
    const char *message = string_member(failure, "message");
    spisor_status answered = refuse_with(refusal, message == NULL ? "the step is malformed" : message,
                                         string_member(failure, "fix"), SPISOR_MALFORMED);
    cJSON_Delete(failure);
    return answered;
}

static spisor_status step(void *context, const forebay_step_argument *argument, forebay_step_answer *answer,
                          spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !has_room(argument->size, sizeof(*argument)) || answer == NULL ||
        !is_empty_answer(answer->size, sizeof(*answer), answer->storage) || !is_step_argument_whole(argument)) {
        return refuse_with(refusal, "the step argument is not whole", "fill forebay_step_argument as it documents",
                           SPISOR_MALFORMED);
    }
    clear_keeping_size(answer, sizeof(*answer));
    cJSON *tree = step_argument_tree(argument);
    if (tree == NULL) {
        return SPISOR_OUT_OF_MEMORY;
    }
    spisor_call call = {tree, NULL};
    cJSON *captured = NULL;
    uint8_t *out = NULL;
    size_t out_len = 0;
    service_capture(&captured);
    int32_t status = routing_step_call(&call, &out, &out_len);
    service_capture(NULL);
    cJSON_Delete(tree);
    if (captured == NULL) {
        return failure_from_text(status, out, out_len, refusal);
    }
    return answer_from_tree(captured, answer, refusal);
}

static void release_step(void *context, forebay_step_answer *answer) {
    (void)context;
    if (answer == NULL || !has_room(answer->size, sizeof(*answer))) {
        return;
    }
    free_step_storage(answer->storage);
    clear_keeping_size(answer, sizeof(*answer));
}

static void release_walk(void *context, forebay_walk *walk) {
    (void)context;
    if (walk == NULL) {
        return;
    }
    cJSON_Delete(walk->walk);
    free(walk);
}

static void release_refusal(void *context, spisor_refusal *refusal) {
    (void)context;
    if (refusal == NULL || !has_room(refusal->size, sizeof(*refusal))) {
        return;
    }
    free(refusal->storage);
    refusal->detail = NULL;
    refusal->fix = NULL;
    refusal->storage = NULL;
}

static const forebay_routing_definition routing_definition = {
    sizeof(forebay_routing_definition), NULL, arrival, step, release_arrival, release_step, release_walk,
    release_refusal,
};

FOREBAY_ROUTING_API const forebay_routing_definition *forebay_library_routing_definition(void) {
    return &routing_definition;
}
