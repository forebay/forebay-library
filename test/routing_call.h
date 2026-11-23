#ifndef FOREBAY_TEST_ROUTING_CALL_H
#define FOREBAY_TEST_ROUTING_CALL_H

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "routing_blocks.h"
#include "vector_reader.h"

/**
 * @file
 * One routing vector as a call through the definition: its JSON argument read into the operation's structs, the
 * call made, and the answer written back as the JSON result the vector expects. `step` has no vectors; its test
 * builds its structs by hand.
 */

typedef enum routing_operation {
    ROUTING_TIERS,
    ROUTING_MODEL_MAP,
    ROUTING_RESOLVE,
    ROUTING_OUTCOME,
    ROUTING_ENVIRONMENT,
    ROUTING_LIMITS,
    ROUTING_PASS_THROUGH,
    ROUTING_REPLY_HEADERS,
    ROUTING_ARRIVAL,
    ROUTING_PORT,
    ROUTING_FAILURE,
    ROUTING_MODEL_ENTRIES,
    ROUTING_CATALOG,
    ROUTING_UNKNOWN
} routing_operation;

typedef struct routing_call {
    routing_operation operation;
    spisor_value_tree value;
    forebay_tiers_argument tiers;
    forebay_model_map_argument model_map;
    forebay_resolve_argument resolve;
    forebay_outcome_argument outcome;
    forebay_environment_argument environment;
    forebay_application application;
    forebay_state_file state_file;
    forebay_pass_through_argument pass_through;
    forebay_reply_headers_argument reply_headers;
    forebay_arrival_argument arrival;
    forebay_port_argument port;
    forebay_port_failure port_failure;
    forebay_failure_argument failure;
    forebay_model_entries_argument model_entries;
    forebay_catalog_argument catalog;
} routing_call;

typedef struct routing_answers {
    spisor_status status;
    spisor_refusal refusal;
    forebay_tiers_answer tiers;
    forebay_model_map_answer model_map;
    forebay_resolve_answer resolve;
    forebay_outcome_answer outcome;
    forebay_environment_answer environment;
    forebay_limits_answer limits;
    forebay_pass_through_answer pass_through;
    forebay_reply_headers_answer reply_headers;
    forebay_arrival_answer arrival;
    forebay_port_answer port;
    forebay_failure_answer failure;
    forebay_model_entries_answer model_entries;
    forebay_catalog_answer catalog;
} routing_answers;

/** Sets the size of a struct its caller zeroed whole, padding included, so an empty one compares byte for byte. */
#define ROUTING_INIT_INTO(target, type) ((target).size = (uint32_t)sizeof(type))

static inline const forebay_routing_definition *routing_definition(void) {
    return forebay_routing_definition_of();
}

static inline routing_operation routing_operation_named(const char *name) {
    static const char *const names[] = {"tiers",       "modelMap",     "resolve", "outcome", "environment", "limits",
                                        "passThrough", "replyHeaders", "arrival", "port",    "failure",
                                        "modelEntries", "catalog"};
    for (int index = 0; index < ROUTING_UNKNOWN; index++) {
        if (name != NULL && strcmp(names[index], name) == 0) {
            return (routing_operation)index;
        }
    }
    return ROUTING_UNKNOWN;
}

static inline void routing_empty_call(routing_call *call) {
    memset(call, 0, sizeof *call);
    ROUTING_INIT_INTO(call->value, spisor_value_tree);
    ROUTING_INIT_INTO(call->tiers, forebay_tiers_argument);
    ROUTING_INIT_INTO(call->model_map, forebay_model_map_argument);
    ROUTING_INIT_INTO(call->resolve, forebay_resolve_argument);
    ROUTING_INIT_INTO(call->outcome, forebay_outcome_argument);
    ROUTING_INIT_INTO(call->environment, forebay_environment_argument);
    ROUTING_INIT_INTO(call->application, forebay_application);
    ROUTING_INIT_INTO(call->state_file, forebay_state_file);
    ROUTING_INIT_INTO(call->pass_through, forebay_pass_through_argument);
    ROUTING_INIT_INTO(call->reply_headers, forebay_reply_headers_argument);
    ROUTING_INIT_INTO(call->arrival, forebay_arrival_argument);
    ROUTING_INIT_INTO(call->port, forebay_port_argument);
    ROUTING_INIT_INTO(call->port_failure, forebay_port_failure);
    ROUTING_INIT_INTO(call->failure, forebay_failure_argument);
    ROUTING_INIT_INTO(call->model_entries, forebay_model_entries_argument);
    ROUTING_INIT_INTO(call->catalog, forebay_catalog_argument);
}

static inline void routing_empty_answers(routing_answers *answers) {
    memset(answers, 0, sizeof *answers);
    ROUTING_INIT_INTO(answers->refusal, spisor_refusal);
    ROUTING_INIT_INTO(answers->tiers, forebay_tiers_answer);
    ROUTING_INIT_INTO(answers->model_map, forebay_model_map_answer);
    ROUTING_INIT_INTO(answers->resolve, forebay_resolve_answer);
    ROUTING_INIT_INTO(answers->outcome, forebay_outcome_answer);
    ROUTING_INIT_INTO(answers->environment, forebay_environment_answer);
    ROUTING_INIT_INTO(answers->limits, forebay_limits_answer);
    ROUTING_INIT_INTO(answers->pass_through, forebay_pass_through_answer);
    ROUTING_INIT_INTO(answers->reply_headers, forebay_reply_headers_answer);
    ROUTING_INIT_INTO(answers->arrival, forebay_arrival_answer);
    ROUTING_INIT_INTO(answers->port, forebay_port_answer);
    ROUTING_INIT_INTO(answers->failure, forebay_failure_answer);
    ROUTING_INIT_INTO(answers->model_entries, forebay_model_entries_answer);
    ROUTING_INIT_INTO(answers->catalog, forebay_catalog_answer);
}

static inline double routing_number_of(const cJSON *object, const char *name) {
    const cJSON *member = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsNumber(member) ? member->valuedouble : 0;
}

static inline const cJSON *routing_member(const cJSON *object, const char *name) {
    return cJSON_GetObjectItemCaseSensitive(object, name);
}

static inline void routing_read_catalog(const cJSON *argument, const forebay_catalog_provider **providers,
                                        size_t *count) {
    *providers = routing_catalog_of(routing_member(argument, "models"), count);
}

static inline void routing_read_application(const cJSON *json, routing_call *call) {
    const cJSON *placeholder = routing_member(json, "placeholder");
    const cJSON *own_login = routing_member(json, "ownLogin");
    const cJSON *state_file = routing_member(own_login, "stateFile");
    const cJSON *variables = routing_member(json, "modelVariables");
    forebay_application *application = &call->application;
    application->base_url_variable = routing_text_of(json, "baseUrlVariable");
    application->placeholder_variable = routing_text_of(placeholder, "variable");
    application->placeholder_value = routing_text_of(placeholder, "value");
    application->own_login_variables = routing_texts_of(routing_member(own_login, "variables"),
                                                        &application->own_login_variable_count);
    call->state_file.directory_variable = routing_text_of(state_file, "directoryVariable");
    call->state_file.name = routing_text_of(state_file, "name");
    call->state_file.member = routing_text_of(state_file, "member");
    application->state_file = state_file == NULL ? NULL : &call->state_file;
    application->tier_model_variable = routing_text_of(variables, "tierModel");
    application->tier_model_name_variable = routing_text_of(variables, "tierModelName");
    application->model_variable = routing_text_of(variables, "model");
}

static inline int routing_read_environment(const cJSON *argument, routing_call *call) {
    forebay_environment_argument *environment = &call->environment;
    environment->slots = routing_slots_of(routing_member(argument, "map"), &environment->slot_count);
    environment->port = (uint32_t)routing_number_of(argument, "port");
    environment->providers_enabled = cJSON_IsTrue(routing_member(argument, "providersEnabled")) ? 1u : 0u;
    environment->user_home = routing_text_of(argument, "userHome");
    routing_read_application(routing_member(argument, "application"), call);
    environment->application = routing_member(argument, "application") == NULL ? NULL : &call->application;
    return routing_pairs_of(routing_member(argument, "environment"), &environment->environment,
                            &environment->environment_count);
}

static inline int routing_read_pass_through(const cJSON *argument, forebay_pass_through_argument *pass) {
    double length = routing_number_of(argument, "bodyLength");
    pass->upstream = routing_text_of(argument, "upstream");
    pass->url = routing_text_of(argument, "url");
    pass->placeholder_authorization = routing_text_of(argument, "placeholderAuthorization");
    if (length < 0 || floor(length) != length) {
        fprintf(stderr, "bodyLength %g cannot be a uint64_t\n", length);
        return 0;
    }
    pass->body_length = (uint64_t)length;
    return routing_pairs_of(routing_member(argument, "headers"), &pass->headers, &pass->header_count);
}

static inline int routing_read_port(const cJSON *argument, routing_call *call) {
    const cJSON *failure = routing_member(argument, "failure");
    call->port.settings_file = routing_text_of(argument, "settingsFile");
    call->port_failure.in_use = cJSON_IsTrue(routing_member(failure, "inUse")) ? 1u : 0u;
    call->port_failure.detail = routing_text_of(failure, "detail");
    call->port.failure = failure == NULL ? NULL : &call->port_failure;
    if (!spisor_vector_value(argument, "value", &call->value)) {
        return 0;
    }
    call->port.value = &call->value.root;
    return 1;
}

static inline int routing_read_failure(const cJSON *argument, forebay_failure_argument *failure) {
    const char *reason = routing_text_of(argument, "reason");
    failure->upstream = routing_text_of(argument, "upstream");
    failure->detail = routing_text_of(argument, "detail");
    if (reason != NULL && strcmp(reason, "upstreamUnreachable") == 0) {
        failure->reason = FOREBAY_UPSTREAM_UNREACHABLE;
    } else if (reason != NULL && strcmp(reason, "requestFailed") == 0) {
        failure->reason = FOREBAY_REQUEST_FAILED;
    } else {
        fprintf(stderr, "the reason %s is no forebay_failure_reason\n", reason == NULL ? "(none)" : reason);
        return 0;
    }
    return 1;
}

static inline int routing_read_model_map(const cJSON *argument, routing_call *call) {
    call->model_map.profile = routing_profile_of(routing_member(argument, "profile"));
    routing_read_catalog(argument, &call->model_map.providers, &call->model_map.provider_count);
    if (routing_member(argument, "settings") == NULL) {
        return 1;
    }
    if (!spisor_vector_value(argument, "settings", &call->value)) {
        return 0;
    }
    call->model_map.settings = &call->value.root;
    return 1;
}

static inline int routing_read_headers_calls(const cJSON *argument, routing_call *call) {
    if (call->operation == ROUTING_OUTCOME) {
        call->outcome.status = (uint32_t)routing_number_of(argument, "status");
        call->outcome.now = routing_number_of(argument, "now");
        call->outcome.has_retry_after = cJSON_IsNumber(routing_member(argument, "retryAfterMilliseconds")) ? 1u : 0u;
        call->outcome.retry_after_milliseconds = routing_number_of(argument, "retryAfterMilliseconds");
        return routing_pairs_of(routing_member(argument, "headers"), &call->outcome.headers,
                                &call->outcome.header_count);
    }
    if (call->operation == ROUTING_REPLY_HEADERS) {
        call->reply_headers.rewritten = cJSON_IsTrue(routing_member(argument, "rewritten")) ? 1u : 0u;
        return routing_pairs_of(routing_member(argument, "headers"), &call->reply_headers.headers,
                                &call->reply_headers.header_count);
    }
    call->arrival.path = routing_text_of(argument, "path");
    call->arrival.port = (uint32_t)routing_number_of(argument, "port");
    return routing_pairs_of(routing_member(argument, "headers"), &call->arrival.headers, &call->arrival.header_count);
}

/**
 * A catalog vector's argument: the `event` by name, `now`, `lastAsked` when a round was ever started, the flags
 * `asking`, `failed` and `roundClosed`, and the `message`.
 */
static inline int routing_read_catalog_event(const cJSON *argument, forebay_catalog_argument *catalog) {
    static const char *const events[] = {"providersChanged", "noModel", "period", "answered", "roundClosed",
                                         "deriveFailed"};
    const char *event = routing_text_of(argument, "event");
    for (uint32_t index = 0; event != NULL && index < sizeof events / sizeof events[0]; index++) {
        catalog->event = strcmp(events[index], event) == 0 ? index + 1 : catalog->event;
    }
    if (catalog->event == 0) {
        fprintf(stderr, "the event %s is no forebay_catalog_event\n", event == NULL ? "(none)" : event);
        return 0;
    }
    catalog->now = routing_number_of(argument, "now");
    catalog->asked = cJSON_IsNumber(routing_member(argument, "lastAsked")) ? 1u : 0u;
    catalog->last_asked = routing_number_of(argument, "lastAsked");
    catalog->asking = cJSON_IsTrue(routing_member(argument, "asking")) ? 1u : 0u;
    catalog->failed = cJSON_IsTrue(routing_member(argument, "failed")) ? 1u : 0u;
    catalog->round_closed = cJSON_IsTrue(routing_member(argument, "roundClosed")) ? 1u : 0u;
    catalog->message = routing_text_of(argument, "message");
    return 1;
}

/** Reads a vector's argument into its operation's structs; 0, having said why, for one the structs cannot hold. */
static inline int routing_read_call(const char *function, const cJSON *argument, routing_call *call) {
    routing_empty_call(call);
    call->operation = routing_operation_named(function);
    switch (call->operation) {
    case ROUTING_TIERS:
        call->tiers.profile = routing_profile_of(routing_member(argument, "profile"));
        routing_read_catalog(argument, &call->tiers.providers, &call->tiers.provider_count);
        return 1;
    case ROUTING_MODEL_MAP:
        return routing_read_model_map(argument, call);
    case ROUTING_RESOLVE:
        call->resolve.profile = routing_profile_of(routing_member(argument, "profile"));
        call->resolve.slots = routing_slots_of(routing_member(argument, "map"), &call->resolve.slot_count);
        routing_read_catalog(argument, &call->resolve.providers, &call->resolve.provider_count);
        call->resolve.requested_model = routing_text_of(argument, "requestedModel");
        return 1;
    case ROUTING_OUTCOME:
    case ROUTING_REPLY_HEADERS:
    case ROUTING_ARRIVAL:
        return routing_read_headers_calls(argument, call);
    case ROUTING_ENVIRONMENT:
        return routing_read_environment(argument, call);
    case ROUTING_LIMITS:
        return 1;
    case ROUTING_PASS_THROUGH:
        return routing_read_pass_through(argument, &call->pass_through);
    case ROUTING_PORT:
        return routing_read_port(argument, call);
    case ROUTING_FAILURE:
        return routing_read_failure(argument, &call->failure);
    case ROUTING_MODEL_ENTRIES:
        call->model_entries.profile = routing_profile_of(routing_member(argument, "profile"));
        routing_read_catalog(argument, &call->model_entries.catalog, &call->model_entries.catalog_count);
        return 1;
    case ROUTING_CATALOG:
        return routing_read_catalog_event(argument, &call->catalog);
    default:
        fprintf(stderr, "the function %s is not one of routing's\n", function == NULL ? "(none)" : function);
        return 0;
    }
}

static inline void routing_release_call(routing_call *call) {
    spisor_value_release(&call->value);
}

static inline void routing_make_call(const routing_call *call, routing_answers *answers) {
    const forebay_routing_definition *routing = routing_definition();
    void *context = routing->context;
    spisor_refusal *refusal = &answers->refusal;
    routing_empty_answers(answers);
    switch (call->operation) {
    case ROUTING_TIERS:
        answers->status = routing->tiers(context, &call->tiers, &answers->tiers, refusal);
        break;
    case ROUTING_MODEL_MAP:
        answers->status = routing->model_map(context, &call->model_map, &answers->model_map, refusal);
        break;
    case ROUTING_RESOLVE:
        answers->status = routing->resolve(context, &call->resolve, &answers->resolve, refusal);
        break;
    case ROUTING_OUTCOME:
        answers->status = routing->outcome(context, &call->outcome, &answers->outcome, refusal);
        break;
    case ROUTING_ENVIRONMENT:
        answers->status = routing->environment(context, &call->environment, &answers->environment, refusal);
        break;
    case ROUTING_LIMITS:
        answers->status = routing->limits(context, &answers->limits, refusal);
        break;
    case ROUTING_PASS_THROUGH:
        answers->status = routing->pass_through(context, &call->pass_through, &answers->pass_through, refusal);
        break;
    case ROUTING_REPLY_HEADERS:
        answers->status = routing->reply_headers(context, &call->reply_headers, &answers->reply_headers, refusal);
        break;
    case ROUTING_ARRIVAL:
        answers->status = routing->arrival(context, &call->arrival, &answers->arrival, refusal);
        break;
    case ROUTING_PORT:
        answers->status = routing->port(context, &call->port, &answers->port, refusal);
        break;
    case ROUTING_MODEL_ENTRIES:
        answers->status = routing->model_entries(context, &call->model_entries, &answers->model_entries, refusal);
        break;
    case ROUTING_CATALOG:
        answers->status = routing->catalog(context, &call->catalog, &answers->catalog, refusal);
        break;
    default:
        answers->status = routing->failure(context, &call->failure, &answers->failure, refusal);
        break;
    }
}

static inline void routing_release_answers(routing_answers *answers) {
    const forebay_routing_definition *routing = routing_definition();
    void *context = routing->context;
    routing->release_tiers_answer(context, &answers->tiers);
    routing->release_model_map_answer(context, &answers->model_map);
    routing->release_resolve_answer(context, &answers->resolve);
    routing->release_environment_answer(context, &answers->environment);
    routing->release_pass_through_answer(context, &answers->pass_through);
    routing->release_reply_headers_answer(context, &answers->reply_headers);
    routing->release_arrival_answer(context, &answers->arrival);
    routing->release_port_answer(context, &answers->port);
    routing->release_failure_answer(context, &answers->failure);
    routing->release_model_entries_answer(context, &answers->model_entries);
    routing->release_catalog_answer(context, &answers->catalog);
    routing->release_refusal(context, &answers->refusal);
}

static inline cJSON *routing_slots_json_of(const forebay_slot *slots, size_t count) {
    cJSON *map = cJSON_CreateObject();
    for (size_t index = 0; index < count; index++) {
        cJSON_AddItemToObject(map, slots[index].slot, routing_chain_json_of(slots[index].chain, slots[index].count));
    }
    return map;
}

static inline cJSON *routing_reply_json_of(uint32_t kind, uint32_t status, const char *message) {
    cJSON *reply = cJSON_CreateObject();
    cJSON_AddStringToObject(reply, "kind", routing_reply_kind_name(kind));
    cJSON_AddNumberToObject(reply, "status", status);
    cJSON_AddStringToObject(reply, "message", message == NULL ? "(null)" : message);
    return reply;
}

static inline cJSON *routing_resolve_json_of(const forebay_resolve_answer *answer) {
    cJSON *result = cJSON_CreateObject();
    cJSON *notices = cJSON_CreateArray();
    for (size_t index = 0; index < answer->notice_count; index++) {
        const char *notice = answer->notices[index] == FOREBAY_HEALED ? "healed" : "model_switched";
        cJSON_AddItemToArray(notices, cJSON_CreateString(notice));
    }
    cJSON_AddItemToObject(result, "chain", routing_chain_json_of(answer->chain, answer->chain_count));
    cJSON_AddItemToObject(result, "notices", notices);
    return result;
}

static inline cJSON *routing_arrival_json_of(const forebay_arrival_answer *answer) {
    cJSON *result = cJSON_CreateObject();
    static const char *const kinds[] = {"(none)", "route", "health", "refuse"};
    cJSON_AddStringToObject(result, "kind", answer->kind <= 3 ? kinds[answer->kind] : "(unknown)");
    if (answer->kind == FOREBAY_ARRIVAL_HEALTH) {
        cJSON_AddNumberToObject(result, "status", answer->status);
        cJSON_AddStringToObject(result, "contentType", answer->content_type);
        cJSON_AddStringToObject(result, "body", answer->body);
    } else if (answer->kind == FOREBAY_ARRIVAL_REFUSE) {
        cJSON_AddItemToObject(result, "reply",
                              routing_reply_json_of(answer->reply_kind, answer->status, answer->message));
    }
    return result;
}

static inline cJSON *routing_limits_json_of(const forebay_limits_answer *answer) {
    cJSON *result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "maxRequestBytes", (double)answer->max_request_bytes);
    cJSON_AddItemToObject(result, "tooLarge",
                          routing_reply_json_of(answer->too_large_reply_kind, answer->too_large_status,
                                                answer->too_large_message));
    cJSON_AddNumberToObject(result, "modelsBudgetMilliseconds", (double)answer->models_budget_milliseconds);
    cJSON_AddNumberToObject(result, "rederiveIntervalMilliseconds", (double)answer->rederive_interval_milliseconds);
    cJSON_AddNumberToObject(result, "refreshEveryMilliseconds", (double)answer->refresh_every_milliseconds);
    return result;
}

static inline cJSON *routing_entries_json_of(const forebay_model_entries_answer *answer) {
    cJSON *entries = cJSON_CreateArray();
    for (size_t index = 0; index < answer->entry_count; index++) {
        const forebay_catalog_model *entry = &answer->entries[index];
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "id", entry->id);
        cJSON_AddStringToObject(json, "name", entry->name);
        if (entry->has_context) {
            cJSON_AddNumberToObject(json, "context", (double)entry->context);
        }
        if (entry->has_output) {
            cJSON_AddNumberToObject(json, "output", (double)entry->output);
        }
        cJSON_AddItemToArray(entries, json);
    }
    return entries;
}

static inline cJSON *routing_catalog_json_of(const forebay_catalog_answer *answer) {
    static const char *const actions[] = {"(none)", "nothing", "ask", "wait"};
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "action", answer->action <= 3 ? actions[answer->action] : "(unknown)");
    if (answer->action == FOREBAY_CATALOG_ASK) {
        cJSON_AddNumberToObject(result, "budgetMilliseconds", (double)answer->budget_milliseconds);
    }
    cJSON_AddBoolToObject(result, "keepLast", answer->keep_last);
    cJSON_AddBoolToObject(result, "rederive", answer->rederive);
    cJSON_AddItemToObject(result, "log", routing_log_json_of(answer->log, answer->log_count));
    return result;
}

static inline cJSON *routing_wrap(const char *name, cJSON *item) {
    cJSON *result = cJSON_CreateObject();
    cJSON_AddItemToObject(result, name, item);
    return result;
}

static inline cJSON *routing_answer_json_of(const routing_call *call, const routing_answers *answers) {
    cJSON *result = NULL;
    switch (call->operation) {
    case ROUTING_TIERS:
        return routing_wrap("tiers", routing_texts_json_of(answers->tiers.tiers, answers->tiers.count));
    case ROUTING_MODEL_MAP:
        result = routing_wrap("map", routing_slots_json_of(answers->model_map.slots, answers->model_map.slot_count));
        cJSON_AddItemToObject(result, "healed",
                              routing_texts_json_of(answers->model_map.healed, answers->model_map.healed_count));
        return result;
    case ROUTING_RESOLVE:
        return routing_resolve_json_of(&answers->resolve);
    case ROUTING_OUTCOME:
        result = cJSON_CreateObject();
        cJSON_AddBoolToObject(result, "rateLimited", answers->outcome.rate_limited);
        if (answers->outcome.has_reset_at) {
            cJSON_AddNumberToObject(result, "resetAt", answers->outcome.reset_at);
        }
        return result;
    case ROUTING_ENVIRONMENT:
        return routing_wrap("environment", routing_pairs_json_of(answers->environment.environment,
                                                                 answers->environment.environment_count));
    case ROUTING_LIMITS:
        return routing_limits_json_of(&answers->limits);
    case ROUTING_PASS_THROUGH:
        result = routing_wrap("headers", routing_pairs_json_of(answers->pass_through.headers,
                                                               answers->pass_through.header_count));
        cJSON_AddStringToObject(result, "target", answers->pass_through.target);
        return result;
    case ROUTING_REPLY_HEADERS:
        return routing_wrap("headers", routing_pairs_json_of(answers->reply_headers.headers,
                                                             answers->reply_headers.header_count));
    case ROUTING_ARRIVAL:
        return routing_arrival_json_of(&answers->arrival);
    case ROUTING_MODEL_ENTRIES:
        return routing_wrap("entries", routing_entries_json_of(&answers->model_entries));
    case ROUTING_CATALOG:
        return routing_catalog_json_of(&answers->catalog);
    case ROUTING_PORT:
        result = cJSON_CreateObject();
        cJSON_AddNumberToObject(result, "port", answers->port.port);
        cJSON_AddStringToObject(result, "address", answers->port.address);
        return result;
    default:
        result = routing_wrap("reply", routing_reply_json_of(answers->failure.reply_kind, answers->failure.status,
                                                              answers->failure.message));
        if (answers->failure.log_count > 0) {
            cJSON_AddItemToObject(result, "log", routing_log_json_of(answers->failure.log, answers->failure.log_count));
        }
        return result;
    }
}

/** The answer as a vector writes it: a refusal as {"refused", "fix"}, a malformed call as {"message", "fix"}. */
static inline cJSON *routing_result_of(const routing_call *call, const routing_answers *answers) {
    if (answers->status == SPISOR_REFUSED || answers->status == SPISOR_MALFORMED) {
        cJSON *result = cJSON_CreateObject();
        cJSON_AddStringToObject(result, answers->status == SPISOR_REFUSED ? "refused" : "message",
                                answers->refusal.detail == NULL ? "" : answers->refusal.detail);
        cJSON_AddStringToObject(result, "fix", answers->refusal.fix == NULL ? "" : answers->refusal.fix);
        return result;
    }
    if (answers->status != SPISOR_OK) {
        return routing_wrap("failedWithStatus", cJSON_CreateNumber(answers->status));
    }
    return routing_answer_json_of(call, answers);
}

#endif
