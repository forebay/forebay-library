#include "routing_definition.h"

#include <string.h>

#include "answer_storage.h"
#include "definition_support.h"
#include "spisor/json_value.h"
#include "routing_answers.h"
#include "routing_arguments.h"
#include "routing_arrival.h"
#include "routing_environment.h"
#include "routing_failure.h"
#include "routing_headers.h"
#include "routing_limits.h"
#include "routing_model_map.h"
#include "routing_outcome.h"
#include "routing_resolve.h"
#include "routing_tiers.h"

#define TIERS_SHAPE SPISOR_SHAPE_THROUGH(forebay_tiers_argument, count)
#define TIERS_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_tiers_answer, storage)
#define MODEL_MAP_SHAPE SPISOR_SHAPE_THROUGH(forebay_model_map_argument, provider_count)
#define MODEL_MAP_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_model_map_answer, storage)
#define RESOLVE_SHAPE SPISOR_SHAPE_THROUGH(forebay_resolve_argument, requested_model)
#define RESOLVE_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_resolve_answer, storage)
#define OUTCOME_SHAPE SPISOR_SHAPE_THROUGH(forebay_outcome_argument, retry_after_milliseconds)
#define OUTCOME_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_outcome_answer, reset_at)
#define LIMITS_SHAPE SPISOR_SHAPE_THROUGH(forebay_limits_argument, size)
#define LIMITS_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_limits_answer, refresh_every_milliseconds)
#define PASS_THROUGH_SHAPE SPISOR_SHAPE_THROUGH(forebay_pass_through_argument, placeholder_authorization)
#define PASS_THROUGH_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_pass_through_answer, storage)
#define REPLY_HEADERS_SHAPE SPISOR_SHAPE_THROUGH(forebay_reply_headers_argument, rewritten)
#define REPLY_HEADERS_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_reply_headers_answer, storage)
#define ARRIVAL_SHAPE SPISOR_SHAPE_THROUGH(forebay_arrival_argument, port)
#define ARRIVAL_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_arrival_answer, storage)
#define PORT_SHAPE SPISOR_SHAPE_THROUGH(forebay_port_argument, failure)
#define PORT_FAILURE_SHAPE SPISOR_SHAPE_THROUGH(forebay_port_failure, detail)
#define PORT_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_port_answer, storage)
#define FAILURE_SHAPE SPISOR_SHAPE_THROUGH(forebay_failure_argument, detail)
#define FAILURE_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_failure_answer, storage)
#define ENVIRONMENT_SHAPE SPISOR_SHAPE_THROUGH(forebay_environment_argument, application)
#define ENVIRONMENT_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_environment_answer, storage)
#define APPLICATION_SHAPE SPISOR_SHAPE_THROUGH(forebay_application, model_variable)
#define STATE_FILE_SHAPE SPISOR_SHAPE_THROUGH(forebay_state_file, member)

spisor_status routing_decide(routing_decision decision, cJSON *argument, routing_answer_filler fill, void *answer,
                             void **storage, size_t shape, spisor_refusal *refusal) {
    service_reply reply;
    memset(&reply, 0, sizeof reply);
    if (argument == NULL) {
        return SPISOR_OUT_OF_MEMORY;
    }
    decision(argument, &reply);
    spisor_status status = routing_served(&reply, refusal);
    if (status == SPISOR_OK && !fill(reply.result, answer)) {
        forebay_storage_release(*storage);
        memset((char *)answer + sizeof(uint32_t), 0, shape - sizeof(uint32_t));
        status = SPISOR_OUT_OF_MEMORY;
    }
    cJSON_Delete(reply.result);
    cJSON_Delete(argument);
    return status;
}

/** A new object holding each named item; NULL, with every item deleted, when one is NULL or memory runs out. */
static cJSON *object_of(size_t count, const char *const *names, cJSON **items) {
    cJSON *object = cJSON_CreateObject();
    for (size_t index = 0; index < count; index++) {
        if (object != NULL && !routing_add_json(object, names[index], items[index])) {
            cJSON_Delete(object);
            object = NULL;
        } else if (object == NULL) {
            cJSON_Delete(items[index]);
        }
    }
    return object;
}

static int fill_tiers(const cJSON *result, void *filled) {
    forebay_tiers_answer *answer = filled;
    return routing_read_texts(&answer->storage, cJSON_GetObjectItemCaseSensitive(result, "tiers"), &answer->tiers,
                              &answer->count);
}

static spisor_status tiers(void *context, const forebay_tiers_argument *argument, forebay_tiers_answer *answer,
                           spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, TIERS_SHAPE) ||
        !SPISOR_IS_EMPTY_COUNTED_ANSWER(answer, TIERS_ANSWER_SHAPE, answer->count)) {
        return spisor_refuse_shape(refusal, "routing's tiers");
    }
    if (!routing_is_profile(argument->profile, 1) || !routing_is_catalog(argument->providers, argument->count)) {
        return forebay_refuse_member(refusal, "the profile or the catalog", "complete, with every text set");
    }
    static const char *const names[] = {"profile", "models"};
    cJSON *items[] = {routing_profile_json(argument->profile),
                      routing_catalog_json(argument->providers, argument->count)};
    return routing_decide(routing_tiers_call, object_of(2, names, items), fill_tiers, answer, &answer->storage,
                          TIERS_ANSWER_SHAPE, refusal);
}

static int fill_model_map(const cJSON *result, void *filled) {
    forebay_model_map_answer *answer = filled;
    return routing_read_slots(&answer->storage, cJSON_GetObjectItemCaseSensitive(result, "map"), &answer->slots,
                              &answer->slot_count) &&
           routing_read_texts(&answer->storage, cJSON_GetObjectItemCaseSensitive(result, "healed"), &answer->healed,
                              &answer->healed_count);
}

static cJSON *settings_json(const spisor_value *settings, spisor_status *status) {
    cJSON *json = NULL;
    *status = spisor_json_of_value(settings, &json);
    return json;
}

static spisor_status model_map(void *context, const forebay_model_map_argument *argument,
                               forebay_model_map_answer *answer, spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, MODEL_MAP_SHAPE) ||
        !SPISOR_IS_EMPTY_COUNTED_ANSWER(answer, MODEL_MAP_ANSWER_SHAPE, answer->slot_count | answer->healed_count)) {
        return spisor_refuse_shape(refusal, "routing's model_map");
    }
    if (!routing_is_profile(argument->profile, 1) ||
        !routing_is_catalog(argument->providers, argument->provider_count)) {
        return forebay_refuse_member(refusal, "the profile or the catalog", "complete, with every text set");
    }
    if (argument->settings == NULL || argument->settings->kind != SPISOR_VALUE_RECORD) {
        return forebay_refuse_member(refusal, "settings", "the user's settings record");
    }
    spisor_status converted = SPISOR_OK;
    static const char *const names[] = {"profile", "settings", "models"};
    cJSON *items[] = {routing_profile_json(argument->profile), settings_json(argument->settings, &converted),
                      routing_catalog_json(argument->providers, argument->provider_count)};
    cJSON *call = object_of(3, names, items);
    if (converted == SPISOR_MALFORMED) {
        cJSON_Delete(call);
        return forebay_refuse_member(refusal, "settings", "a well-formed spisor_value");
    }
    return routing_decide(routing_model_map_call, call, fill_model_map, answer, &answer->storage,
                          MODEL_MAP_ANSWER_SHAPE, refusal);
}

static uint32_t notice_of(const char *notice) {
    return notice != NULL && strcmp(notice, "healed") == 0 ? FOREBAY_HEALED : FOREBAY_MODEL_SWITCHED;
}

static int fill_resolve(const cJSON *result, void *filled) {
    forebay_resolve_answer *answer = filled;
    const cJSON *notices = cJSON_GetObjectItemCaseSensitive(result, "notices");
    size_t count = (size_t)cJSON_GetArraySize(notices);
    uint32_t *read = forebay_storage_array(&answer->storage, count, sizeof *read);
    const cJSON *notice = NULL;
    size_t index = 0;
    if (count > 0 && read == NULL) {
        return 0;
    }
    cJSON_ArrayForEach(notice, notices) {
        read[index++] = notice_of(cJSON_GetStringValue(notice));
    }
    answer->notices = read;
    answer->notice_count = count;
    return routing_read_assignments(&answer->storage, cJSON_GetObjectItemCaseSensitive(result, "chain"),
                                    &answer->chain, &answer->chain_count);
}

static spisor_status resolve(void *context, const forebay_resolve_argument *argument, forebay_resolve_answer *answer,
                             spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, RESOLVE_SHAPE) ||
        !SPISOR_IS_EMPTY_COUNTED_ANSWER(answer, RESOLVE_ANSWER_SHAPE, answer->chain_count | answer->notice_count)) {
        return spisor_refuse_shape(refusal, "routing's resolve");
    }
    if (!routing_is_profile(argument->profile, 0) || !routing_are_slots(argument->slots, argument->slot_count) ||
        !routing_is_catalog(argument->providers, argument->provider_count) || argument->requested_model == NULL) {
        return forebay_refuse_member(refusal, "the profile, the slots, the catalog or requested_model",
                                     "complete, with every text set");
    }
    static const char *const names[] = {"profile", "map", "models", "requestedModel"};
    cJSON *items[] = {routing_profile_json(argument->profile),
                      routing_slots_json(argument->slots, argument->slot_count),
                      routing_catalog_json(argument->providers, argument->provider_count),
                      cJSON_CreateString(argument->requested_model)};
    return routing_decide(routing_resolve_call, object_of(4, names, items), fill_resolve, answer, &answer->storage,
                          RESOLVE_ANSWER_SHAPE, refusal);
}

static int fill_outcome(const cJSON *result, void *filled) {
    forebay_outcome_answer *answer = filled;
    const cJSON *reset_at = cJSON_GetObjectItemCaseSensitive(result, "resetAt");
    answer->rate_limited = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result, "rateLimited")) ? 1u : 0u;
    answer->has_reset_at = cJSON_IsNumber(reset_at) ? 1u : 0u;
    answer->reset_at = cJSON_IsNumber(reset_at) ? reset_at->valuedouble : 0;
    return 1;
}

static spisor_status outcome(void *context, const forebay_outcome_argument *argument, forebay_outcome_answer *answer,
                             spisor_refusal *refusal) {
    void *storage = NULL;
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, OUTCOME_SHAPE) || answer == NULL ||
        !spisor_has_shape(answer->size, OUTCOME_ANSWER_SHAPE)) {
        return spisor_refuse_shape(refusal, "routing's outcome");
    }
    if (!routing_are_pairs(argument->headers, argument->header_count) || !forebay_is_finite(argument->now) ||
        !forebay_is_finite(argument->retry_after_milliseconds)) {
        return forebay_refuse_member(refusal, "the headers or a number", "complete, every number finite");
    }
    static const char *const names[] = {"status", "headers", "now", "retryAfterMilliseconds"};
    cJSON *items[] = {cJSON_CreateNumber(argument->status),
                      routing_pairs_json(argument->headers, argument->header_count),
                      cJSON_CreateNumber(argument->now),
                      argument->has_retry_after ? cJSON_CreateNumber(argument->retry_after_milliseconds) : NULL};
    return routing_decide(routing_outcome_call, object_of(argument->has_retry_after ? 4 : 3, names, items),
                          fill_outcome, answer, &storage, OUTCOME_ANSWER_SHAPE, refusal);
}

static spisor_status limits(void *context, const forebay_limits_argument *argument, forebay_limits_answer *answer,
                            spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, LIMITS_SHAPE) || answer == NULL ||
        !spisor_has_shape(answer->size, LIMITS_ANSWER_SHAPE)) {
        return spisor_refuse_shape(refusal, "routing's limits");
    }
    routing_read_limits(answer);
    return SPISOR_OK;
}

static int fill_pass_through(const cJSON *result, void *filled) {
    forebay_pass_through_answer *answer = filled;
    return routing_read_text(&answer->storage, result, "target", &answer->target) &&
           routing_read_pairs(&answer->storage, cJSON_GetObjectItemCaseSensitive(result, "headers"), &answer->headers,
                              &answer->header_count);
}

static spisor_status pass_through(void *context, const forebay_pass_through_argument *argument,
                                  forebay_pass_through_answer *answer, spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, PASS_THROUGH_SHAPE) ||
        !SPISOR_IS_EMPTY_COUNTED_ANSWER(answer, PASS_THROUGH_ANSWER_SHAPE, answer->header_count)) {
        return spisor_refuse_shape(refusal, "routing's pass_through");
    }
    if (argument->upstream == NULL || argument->url == NULL || argument->placeholder_authorization == NULL ||
        !routing_are_pairs(argument->headers, argument->header_count)) {
        return forebay_refuse_member(refusal, "upstream, url, the headers or placeholder_authorization",
                                     "complete, with every text set");
    }
    static const char *const names[] = {"upstream", "url", "headers", "bodyLength", "placeholderAuthorization"};
    cJSON *items[] = {cJSON_CreateString(argument->upstream), cJSON_CreateString(argument->url),
                      routing_pairs_json(argument->headers, argument->header_count),
                      cJSON_CreateNumber((double)argument->body_length),
                      cJSON_CreateString(argument->placeholder_authorization)};
    return routing_decide(routing_pass_through_call, object_of(5, names, items), fill_pass_through, answer,
                          &answer->storage, PASS_THROUGH_ANSWER_SHAPE, refusal);
}

static int fill_reply_headers(const cJSON *result, void *filled) {
    forebay_reply_headers_answer *answer = filled;
    return routing_read_pairs(&answer->storage, cJSON_GetObjectItemCaseSensitive(result, "headers"), &answer->headers,
                              &answer->header_count);
}

static spisor_status reply_headers(void *context, const forebay_reply_headers_argument *argument,
                                   forebay_reply_headers_answer *answer, spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, REPLY_HEADERS_SHAPE) ||
        !SPISOR_IS_EMPTY_COUNTED_ANSWER(answer, REPLY_HEADERS_ANSWER_SHAPE, answer->header_count)) {
        return spisor_refuse_shape(refusal, "routing's reply_headers");
    }
    if (!routing_are_pairs(argument->headers, argument->header_count)) {
        return forebay_refuse_member(refusal, "the headers", "pairs, each with a name and a value");
    }
    static const char *const names[] = {"headers", "rewritten"};
    cJSON *items[] = {routing_pairs_json(argument->headers, argument->header_count),
                      cJSON_CreateBool(argument->rewritten != 0)};
    return routing_decide(routing_reply_headers_call, object_of(2, names, items), fill_reply_headers, answer,
                          &answer->storage, REPLY_HEADERS_ANSWER_SHAPE, refusal);
}

static spisor_status arrival(void *context, const forebay_arrival_argument *argument, forebay_arrival_answer *answer,
                             spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, ARRIVAL_SHAPE) ||
        !SPISOR_IS_EMPTY_ANSWER(answer, ARRIVAL_ANSWER_SHAPE)) {
        return spisor_refuse_shape(refusal, "routing's arrival");
    }
    if (argument->path == NULL || !routing_are_pairs(argument->headers, argument->header_count)) {
        return forebay_refuse_member(refusal, "path or the headers", "complete, with every text set");
    }
    if (argument->port == 0 || argument->port > 65535) {
        return forebay_refuse_member(refusal, "port", "a port number from 1 to 65535");
    }
    return routing_decide_arrival(argument, answer);
}

static int fill_port(const cJSON *result, void *filled) {
    forebay_port_answer *answer = filled;
    answer->port = (uint32_t)routing_number(result, "port");
    return routing_read_text(&answer->storage, result, "address", &answer->address);
}

static cJSON *port_failure_json(const forebay_port_failure *failure) {
    cJSON *json = cJSON_CreateObject();
    if (json == NULL || cJSON_AddBoolToObject(json, "inUse", failure->in_use != 0) == NULL ||
        cJSON_AddStringToObject(json, "detail", failure->detail) == NULL) {
        cJSON_Delete(json);
        return NULL;
    }
    return json;
}

static int is_port_failure(const forebay_port_failure *failure) {
    return failure == NULL || (spisor_has_shape(failure->size, PORT_FAILURE_SHAPE) && failure->detail != NULL);
}

static spisor_status port(void *context, const forebay_port_argument *argument, forebay_port_answer *answer,
                          spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, PORT_SHAPE) ||
        !SPISOR_IS_EMPTY_ANSWER(answer, PORT_ANSWER_SHAPE)) {
        return spisor_refuse_shape(refusal, "routing's port");
    }
    if (argument->value == NULL || argument->settings_file == NULL || !is_port_failure(argument->failure)) {
        return forebay_refuse_member(refusal, "value, settings_file or failure", "complete, with every text set");
    }
    spisor_status converted = SPISOR_OK;
    static const char *const names[] = {"value", "settingsFile", "failure"};
    cJSON *items[] = {settings_json(argument->value, &converted), cJSON_CreateString(argument->settings_file),
                      argument->failure == NULL ? NULL : port_failure_json(argument->failure)};
    cJSON *call = object_of(argument->failure == NULL ? 2 : 3, names, items);
    if (converted == SPISOR_MALFORMED) {
        cJSON_Delete(call);
        return forebay_refuse_member(refusal, "value", "a well-formed spisor_value");
    }
    return routing_decide(routing_port_call, call, fill_port, answer, &answer->storage, PORT_ANSWER_SHAPE, refusal);
}

static int fill_failure(const cJSON *result, void *filled) {
    forebay_failure_answer *answer = filled;
    const cJSON *reply = cJSON_GetObjectItemCaseSensitive(result, "reply");
    answer->reply_kind = routing_reply_kind(reply);
    answer->status = (uint32_t)routing_number(reply, "status");
    return routing_read_text(&answer->storage, reply, "message", &answer->message) &&
           routing_read_log(&answer->storage, cJSON_GetObjectItemCaseSensitive(result, "log"), &answer->log,
                            &answer->log_count);
}

static int is_failure(const forebay_failure_argument *argument) {
    return argument->detail != NULL &&
           (argument->reason == FOREBAY_REQUEST_FAILED ||
            (argument->reason == FOREBAY_UPSTREAM_UNREACHABLE && argument->upstream != NULL));
}

static spisor_status failure(void *context, const forebay_failure_argument *argument, forebay_failure_answer *answer,
                             spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, FAILURE_SHAPE) ||
        !SPISOR_IS_EMPTY_COUNTED_ANSWER(answer, FAILURE_ANSWER_SHAPE, answer->log_count)) {
        return spisor_refuse_shape(refusal, "routing's failure");
    }
    if (!is_failure(argument)) {
        return forebay_refuse_member(refusal, "reason, upstream or detail",
                                     "a forebay_failure_reason with its detail and, when unreachable, its upstream");
    }
    int unreachable = argument->reason == FOREBAY_UPSTREAM_UNREACHABLE;
    static const char *const names[] = {"reason", "detail", "upstream"};
    cJSON *items[] = {cJSON_CreateString(unreachable ? "upstreamUnreachable" : "requestFailed"),
                      cJSON_CreateString(argument->detail),
                      unreachable ? cJSON_CreateString(argument->upstream) : NULL};
    return routing_decide(routing_failure_call, object_of(unreachable ? 3 : 2, names, items), fill_failure, answer,
                          &answer->storage, FAILURE_ANSWER_SHAPE, refusal);
}

static int fill_environment(const cJSON *result, void *filled) {
    forebay_environment_answer *answer = filled;
    return routing_read_pairs(&answer->storage, cJSON_GetObjectItemCaseSensitive(result, "environment"),
                              &answer->environment, &answer->variable_count);
}

static int is_application(const forebay_application *application) {
    const forebay_state_file *state_file = application == NULL ? NULL : application->state_file;
    return application != NULL && spisor_has_shape(application->size, APPLICATION_SHAPE) &&
           application->base_url_variable != NULL &&
           application->placeholder_variable != NULL && application->placeholder_value != NULL &&
           routing_are_texts(application->own_login_variables, application->own_login_variable_count) &&
           (state_file == NULL || (spisor_has_shape(state_file->size, STATE_FILE_SHAPE) && state_file->name != NULL &&
                                   state_file->member != NULL));
}

static int add_optional_text(cJSON *object, const char *name, const char *text) {
    return text == NULL || cJSON_AddStringToObject(object, name, text) != NULL;
}

static cJSON *state_file_json(const forebay_state_file *state_file) {
    cJSON *json = cJSON_CreateObject();
    if (json == NULL || !add_optional_text(json, "directoryVariable", state_file->directory_variable) ||
        cJSON_AddStringToObject(json, "name", state_file->name) == NULL ||
        cJSON_AddStringToObject(json, "member", state_file->member) == NULL) {
        cJSON_Delete(json);
        return NULL;
    }
    return json;
}

static cJSON *own_login_json(const forebay_application *application) {
    cJSON *json = cJSON_CreateObject();
    if (json == NULL ||
        !routing_add_json(json, "variables", routing_texts_json(application->own_login_variables,
                                                                application->own_login_variable_count)) ||
        (application->state_file != NULL &&
         !routing_add_json(json, "stateFile", state_file_json(application->state_file)))) {
        cJSON_Delete(json);
        return NULL;
    }
    return json;
}

static int has_model_variables(const forebay_application *application) {
    return application->tier_model_variable != NULL || application->tier_model_name_variable != NULL ||
           application->model_variable != NULL;
}

static cJSON *model_variables_json(const forebay_application *application) {
    cJSON *json = cJSON_CreateObject();
    if (json == NULL || !add_optional_text(json, "tierModel", application->tier_model_variable) ||
        !add_optional_text(json, "tierModelName", application->tier_model_name_variable) ||
        !add_optional_text(json, "model", application->model_variable)) {
        cJSON_Delete(json);
        return NULL;
    }
    return json;
}

static cJSON *application_json(const forebay_application *application) {
    static const char *const names[] = {"baseUrlVariable", "placeholder", "ownLogin", "modelVariables"};
    static const char *const placeholder_names[] = {"variable", "value"};
    cJSON *placeholder_items[] = {cJSON_CreateString(application->placeholder_variable),
                                  cJSON_CreateString(application->placeholder_value)};
    cJSON *items[] = {cJSON_CreateString(application->base_url_variable),
                      object_of(2, placeholder_names, placeholder_items), own_login_json(application),
                      has_model_variables(application) ? model_variables_json(application) : NULL};
    return object_of(has_model_variables(application) ? 4 : 3, names, items);
}

static spisor_status environment(void *context, const forebay_environment_argument *argument,
                                 forebay_environment_answer *answer, spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, ENVIRONMENT_SHAPE) ||
        !SPISOR_IS_EMPTY_COUNTED_ANSWER(answer, ENVIRONMENT_ANSWER_SHAPE, answer->variable_count)) {
        return spisor_refuse_shape(refusal, "routing's environment");
    }
    if (!routing_are_slots(argument->slots, argument->slot_count) || argument->user_home == NULL ||
        !routing_are_pairs(argument->environment, argument->variable_count) ||
        !is_application(argument->application)) {
        return forebay_refuse_member(refusal, "the slots, the environment, user_home or the application",
                                     "complete, with every text the application needs set");
    }
    if (argument->port < 1 || argument->port > 65535) {
        return forebay_refuse_member(refusal, "port", "a port number from 1 to 65535");
    }
    static const char *const names[] = {"map", "port", "providersEnabled", "environment", "userHome", "application"};
    cJSON *items[] = {routing_slots_json(argument->slots, argument->slot_count), cJSON_CreateNumber(argument->port),
                      cJSON_CreateBool(argument->providers_enabled != 0),
                      routing_pairs_json(argument->environment, argument->variable_count),
                      cJSON_CreateString(argument->user_home), application_json(argument->application)};
    return routing_decide(routing_environment_call, object_of(6, names, items), fill_environment, answer,
                          &answer->storage, ENVIRONMENT_ANSWER_SHAPE, refusal);
}

#define RELEASE_ANSWER(name, type, shape)                              \
    static void name(void *context, type *answer) {                    \
        (void)context;                                                 \
        if (answer != NULL && spisor_has_shape(answer->size, shape)) { \
            forebay_storage_release(answer->storage);                  \
            SPISOR_CLEAR_ANSWER(answer, shape);                        \
        }                                                              \
    }

RELEASE_ANSWER(release_tiers_answer, forebay_tiers_answer, TIERS_ANSWER_SHAPE)
RELEASE_ANSWER(release_model_map_answer, forebay_model_map_answer, MODEL_MAP_ANSWER_SHAPE)
RELEASE_ANSWER(release_resolve_answer, forebay_resolve_answer, RESOLVE_ANSWER_SHAPE)
RELEASE_ANSWER(release_arrival_answer, forebay_arrival_answer, ARRIVAL_ANSWER_SHAPE)
RELEASE_ANSWER(release_pass_through_answer, forebay_pass_through_answer, PASS_THROUGH_ANSWER_SHAPE)
RELEASE_ANSWER(release_reply_headers_answer, forebay_reply_headers_answer, REPLY_HEADERS_ANSWER_SHAPE)
RELEASE_ANSWER(release_port_answer, forebay_port_answer, PORT_ANSWER_SHAPE)
RELEASE_ANSWER(release_failure_answer, forebay_failure_answer, FAILURE_ANSWER_SHAPE)
RELEASE_ANSWER(release_environment_answer, forebay_environment_answer, ENVIRONMENT_ANSWER_SHAPE)

static const forebay_routing_definition definition = {
    sizeof(forebay_routing_definition),
    NULL,
    tiers,
    model_map,
    resolve,
    outcome,
    routing_step,
    limits,
    pass_through,
    reply_headers,
    arrival,
    port,
    failure,
    environment,
    release_tiers_answer,
    release_model_map_answer,
    release_resolve_answer,
    routing_release_step_answer,
    release_arrival_answer,
    routing_release_walk,
    release_pass_through_answer,
    release_reply_headers_answer,
    release_port_answer,
    release_failure_answer,
    release_environment_answer,
    spisor_release_refusal,
};

FOREBAY_DEFINITION_API const void *forebay_library_routing_definition(void) {
    return &definition;
}
