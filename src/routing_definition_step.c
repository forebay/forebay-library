#include <string.h>

#include "answer_storage.h"
#include "definition_support.h"
#include "routing_answers.h"
#include "routing_arguments.h"
#include "routing_definition.h"
#include "routing_step.h"
#include "spisor/allocation.h"

#define STEP_SHAPE SPISOR_SHAPE_THROUGH(forebay_step_argument, report)
#define STEP_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_step_answer, storage)
#define REPORT_SHAPE SPISOR_SHAPE_THROUGH(forebay_report, now)

struct forebay_walk {
    cJSON *tree;
};

static uint32_t step_kind_of(const char *kind) {
    static const char *const kinds[] = {"passThrough", "refreshModels", "callProvider", "reply", "endStream", "done"};
    for (uint32_t index = 0; kind != NULL && index < 6; index++) {
        if (strcmp(kinds[index], kind) == 0) {
            return FOREBAY_STEP_PASS_THROUGH + index;
        }
    }
    return 0;
}

static int fill_reply(void **storage, const cJSON *reply, forebay_step_answer *answer) {
    const cJSON *reset_at = cJSON_GetObjectItemCaseSensitive(reply, "resetAt");
    answer->reply_kind = routing_reply_kind(reply);
    answer->status = (uint32_t)routing_number(reply, "status");
    answer->has_reset_at = cJSON_IsNumber(reset_at) ? 1u : 0u;
    answer->reset_at = cJSON_IsNumber(reset_at) ? reset_at->valuedouble : 0;
    return routing_read_text(storage, reply, "message", &answer->message) &&
           routing_read_pairs(storage, cJSON_GetObjectItemCaseSensitive(reply, "headers"), &answer->headers,
                              &answer->header_count);
}

static int fill_kind(void **storage, const cJSON *step, forebay_step_answer *answer) {
    const cJSON *event = cJSON_GetObjectItemCaseSensitive(step, "event");
    const cJSON *reply = cJSON_GetObjectItemCaseSensitive(step, "reply");
    answer->kind = step_kind_of(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(step, "kind")));
    if (!routing_read_text(storage, step, "provider", &answer->provider) ||
        !routing_read_text(storage, step, "model", &answer->model)) {
        return 0;
    }
    if (event != NULL) {
        return routing_read_text(storage, event, "errorType", &answer->error_type) &&
               routing_read_text(storage, event, "message", &answer->message);
    }
    return reply == NULL || fill_reply(storage, reply, answer);
}

/** Takes the result's walk into a handle of its own; 0 when memory runs out, the walk then left in the result. */
static int take_walk(cJSON *result, forebay_step_answer *answer) {
    if (cJSON_GetObjectItemCaseSensitive(result, "walk") == NULL) {
        return 1;
    }
    forebay_walk *walk = spisor_memory_allocate(sizeof *walk);
    if (walk == NULL) {
        return 0;
    }
    walk->tree = cJSON_DetachItemFromObjectCaseSensitive(result, "walk");
    answer->walk = walk;
    return 1;
}

static int fill_step(const cJSON *result, void *filled) {
    forebay_step_answer *answer = filled;
    return fill_kind(&answer->storage, cJSON_GetObjectItemCaseSensitive(result, "step"), answer) &&
           routing_read_log(&answer->storage, cJSON_GetObjectItemCaseSensitive(result, "log"), &answer->log,
                            &answer->log_count) &&
           take_walk((cJSON *)result, answer);
}

static cJSON *request_json(const forebay_step_argument *argument) {
    cJSON *request = cJSON_CreateObject();
    if (request == NULL ||
        (argument->model != NULL && cJSON_AddStringToObject(request, "model", argument->model) == NULL) ||
        (argument->pass_through && cJSON_AddTrueToObject(request, "passThrough") == NULL)) {
        cJSON_Delete(request);
        return NULL;
    }
    return request;
}

static cJSON *wording_json(const forebay_step_argument *argument) {
    cJSON *wording = cJSON_CreateObject();
    if (wording == NULL || cJSON_AddStringToObject(wording, "loginCommand", argument->login_command) == NULL ||
        cJSON_AddStringToObject(wording, "settingsFile", argument->settings_file) == NULL) {
        cJSON_Delete(wording);
        return NULL;
    }
    return wording;
}

static cJSON *first_step_json(const forebay_step_argument *argument) {
    cJSON *call = cJSON_CreateObject();
    if (call == NULL ||
        !routing_add_json(call, "providers", routing_texts_json(argument->providers, argument->provider_count)) ||
        !routing_add_json(call, "profile", routing_profile_json(argument->profile)) ||
        !routing_add_json(call, "map", routing_slots_json(argument->slots, argument->slot_count)) ||
        !routing_add_json(call, "models", routing_catalog_json(argument->catalog, argument->catalog_count)) ||
        !routing_add_json(call, "request", request_json(argument)) ||
        !routing_add_json(call, "refreshed", cJSON_CreateBool(argument->refreshed != 0)) ||
        !routing_add_json(call, "wording", wording_json(argument))) {
        cJSON_Delete(call);
        return NULL;
    }
    return call;
}

static cJSON *report_json(const forebay_report *report) {
    static const char *const kinds[] = {"response", "streamed", "streamFailed", "thrown", "failure"};
    cJSON *json = cJSON_CreateObject();
    if (json == NULL || cJSON_AddStringToObject(json, "kind", kinds[report->kind - FOREBAY_REPORT_RESPONSE]) == NULL ||
        cJSON_AddBoolToObject(json, "afterBytes", report->after_bytes != 0) == NULL ||
        (report->message != NULL && cJSON_AddStringToObject(json, "message", report->message) == NULL) ||
        cJSON_AddNumberToObject(json, "status", report->status) == NULL ||
        !routing_add_json(json, "headers", routing_pairs_json(report->headers, report->header_count)) ||
        (report->has_retry_after &&
         cJSON_AddNumberToObject(json, "retryAfterMilliseconds", report->retry_after_milliseconds) == NULL) ||
        cJSON_AddNumberToObject(json, "now", report->now) == NULL) {
        cJSON_Delete(json);
        return NULL;
    }
    return json;
}

/** @implNote The walk goes in by reference: routing's decisions only read it, which keeps it unchanged. */
static cJSON *later_step_json(const forebay_step_argument *argument) {
    cJSON *call = cJSON_CreateObject();
    if (call == NULL || !routing_add_json(call, "walk", cJSON_CreateObjectReference(argument->walk->tree->child)) ||
        !routing_add_json(call, "providers", routing_texts_json(argument->providers, argument->provider_count)) ||
        !routing_add_json(call, "report", report_json(argument->report))) {
        cJSON_Delete(call);
        return NULL;
    }
    return call;
}

static int is_http_status(uint32_t status) {
    return status == 0 || (status >= 100 && status <= 599);
}

static int is_report(const forebay_report *report) {
    return report != NULL && spisor_has_shape(report->size, REPORT_SHAPE) && report->kind >= FOREBAY_REPORT_RESPONSE &&
           report->kind <= FOREBAY_REPORT_FAILURE && routing_are_pairs(report->headers, report->header_count) &&
           is_http_status(report->status) && forebay_is_finite(report->retry_after_milliseconds) &&
           forebay_is_finite(report->now);
}

static int is_first_step(const forebay_step_argument *argument) {
    return routing_is_profile(argument->profile, 0) && routing_are_slots(argument->slots, argument->slot_count) &&
           routing_is_catalog(argument->catalog, argument->catalog_count) && argument->login_command != NULL &&
           argument->settings_file != NULL;
}

spisor_status routing_step(void *context, const forebay_step_argument *argument, forebay_step_answer *answer,
                           spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, STEP_SHAPE) ||
        !SPISOR_IS_EMPTY_COUNTED_ANSWER(answer, STEP_ANSWER_SHAPE, answer->header_count | answer->log_count) ||
        answer->walk != NULL) {
        return spisor_refuse_shape(refusal, "routing's step");
    }
    if (!routing_are_texts(argument->providers, argument->provider_count) ||
        (argument->walk == NULL ? !is_first_step(argument) : !is_report(argument->report))) {
        return forebay_refuse_member(refusal, argument->walk == NULL ? "a first step's members" : "report",
                                     argument->walk == NULL ? "complete, with every text set"
                                                            : "a forebay_report of a kind the header names, with "
                                                              "every number in range");
    }
    cJSON *call = argument->walk == NULL ? first_step_json(argument) : later_step_json(argument);
    return routing_decide(routing_step_call, call, fill_step, answer, &answer->storage, STEP_ANSWER_SHAPE, refusal);
}

void routing_release_walk(void *context, forebay_walk *walk) {
    (void)context;
    if (walk != NULL) {
        cJSON_Delete(walk->tree);
        spisor_memory_release(walk);
    }
}

void routing_release_step_answer(void *context, forebay_step_answer *answer) {
    (void)context;
    if (answer != NULL && spisor_has_shape(answer->size, STEP_ANSWER_SHAPE)) {
        forebay_storage_release(answer->storage);
        SPISOR_CLEAR_ANSWER(answer, STEP_ANSWER_SHAPE);
    }
}
