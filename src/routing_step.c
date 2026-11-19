#include "routing_step.h"

#include "spisor/allocation.h"

#include <stdlib.h>
#include <string.h>

#include "routing_outcome.h"
#include "routing_resolve.h"
#include "routing_step_answer.h"

#define BAD_GATEWAY 502
#define SERVICE_UNAVAILABLE 503
#define NO_MESSAGE "no message given"

typedef int32_t (*report_reader)(step_walk *walk, const cJSON *providers, const cJSON *report, service_reply *reply);

typedef struct report_reading {
    const char *kind;
    report_reader read;
} report_reading;

static const char *what_follows(const step_walk *walk) {
    return walk->index == cJSON_GetArraySize(walk->chain) - 1 ? "and no chain entry is left to serve"
                                                               : "so the next chain entry serves";
}

static int is_running(const cJSON *providers, const char *provider) {
    const cJSON *id = NULL;
    cJSON_ArrayForEach(id, providers) {
        if (strcmp(id->valuestring, provider) == 0) {
            return 1;
        }
    }
    return 0;
}

static cJSON *exhausted(step_walk *walk) {
    if (walk->limited_headers != NULL) {
        return routing_answer_of(walk, routing_rate_limited_step(walk), 0);
    }
    char *message =
        routing_formatted("no chain entry could serve the request: %s", walk->failure == NULL ? "" : walk->failure);
    cJSON *step = message == NULL ? NULL : routing_reply_step("proxyError", BAD_GATEWAY, message);
    spisor_memory_release(message);
    return routing_answer_of(walk, step, 0);
}

static int skip_missing(step_walk *walk, int index) {
    char *message = routing_formatted("no running plugin provides %s, which the model map names for %s",
                                      routing_walk_entry_member(walk, index, "provider"),
                                      routing_walk_entry_member(walk, index, "model"));
    return routing_walk_remember_failure(walk, message) && routing_walk_log(walk, "warn", "%s", walk->failure);
}

/** Calls the first entry from `from` on whose provider runs; every entry before it is logged and skipped. */
static cJSON *next_entry(step_walk *walk, const cJSON *providers, int from) {
    int size = cJSON_GetArraySize(walk->chain);
    for (int index = from; index < size; index++) {
        if (is_running(providers, routing_walk_entry_member(walk, index, "provider"))) {
            walk->index = index;
            return routing_answer_of(walk, routing_call_provider_step(walk), 1);
        }
        if (!skip_missing(walk, index)) {
            return NULL;
        }
    }
    return exhausted(walk);
}

/** @implNote A fallback serving is always logged: the user asked for one model and is answered by another. */
static cJSON *served(step_walk *walk, cJSON *step) {
    if (walk->index > 0 &&
        !routing_walk_log(walk, "warn", "%s served %s in place of %s",
                          routing_walk_entry_member(walk, walk->index, "model"), walk->requested,
                          routing_walk_entry_member(walk, 0, "model"))) {
        cJSON_Delete(step);
        return NULL;
    }
    return routing_answer_of(walk, step, 0);
}

static int32_t read_served(step_walk *walk, const cJSON *providers, const cJSON *report, service_reply *reply) {
    (void)providers;
    (void)report;
    return service_answer(served(walk, routing_kind_step("done")), reply);
}

static const char *message_of(const cJSON *report) {
    const cJSON *message = cJSON_GetObjectItemCaseSensitive(report, "message");
    return message == NULL ? NO_MESSAGE : message->valuestring;
}

static int32_t read_thrown(step_walk *walk, const cJSON *providers, const cJSON *report, service_reply *reply) {
    const char *message = message_of(report);
    cJSON *answer = NULL;
    if (routing_walk_remember_failure(walk, routing_formatted("%s", message)) &&
        routing_walk_log(walk, "warn", "%s failed %s, %s %s: %s",
                         routing_walk_entry_member(walk, walk->index, "provider"),
                         routing_walk_entry_member(walk, walk->index, "model"), what_follows(walk), walk->requested,
                         message)) {
        answer = next_entry(walk, providers, walk->index + 1);
    }
    return service_answer(answer, reply);
}

static cJSON *stream_failed_before_bytes(step_walk *walk, const cJSON *providers, const char *message) {
    if (!routing_walk_remember_failure(walk, routing_formatted("%s", message)) ||
        !routing_walk_log(walk, "warn", "the stream of %s failed on its first event, %s %s: %s",
                          routing_walk_entry_member(walk, walk->index, "model"), what_follows(walk), walk->requested,
                          message)) {
        return NULL;
    }
    return next_entry(walk, providers, walk->index + 1);
}

static cJSON *stream_failed_after_bytes(step_walk *walk, const char *message) {
    if (!routing_walk_log(walk, "warn", "a stream failed after its first event: %s", message)) {
        return NULL;
    }
    return served(walk, routing_end_stream_step(message));
}

static int32_t read_stream_failed(step_walk *walk, const cJSON *providers, const cJSON *report, service_reply *reply) {
    int32_t status = SERVICE_SUCCESS;
    const cJSON *after_bytes = NULL;
    if (!service_require_member(report, "the report member", "afterBytes", service_is_boolean, "a boolean",
                                &after_bytes, reply, &status)) {
        return status;
    }
    const char *message = message_of(report);
    cJSON *answer = cJSON_IsTrue(after_bytes) ? stream_failed_after_bytes(walk, message)
                                              : stream_failed_before_bytes(walk, providers, message);
    return service_answer(answer, reply);
}

static int read_failure_members(const cJSON *report, service_reply *reply, int32_t *status) {
    const cJSON *member = NULL;
    return service_require_member(report, "the report member", "status", service_is_number, "a number", &member, reply,
                                  status) &&
           service_require_member(report, "the report member", "headers", service_is_object, "an object", &member,
                                  reply, status) &&
           service_require_member(report, "the report member", "now", service_is_number, "a number", &member, reply,
                                  status);
}

static cJSON *rate_limited(step_walk *walk, const cJSON *providers, const cJSON *headers, const cJSON *outcome) {
    const cJSON *reset_at = cJSON_GetObjectItemCaseSensitive(outcome, "resetAt");
    routing_walk_remember_limit(walk, headers, reset_at == NULL ? 0 : reset_at->valuedouble);
    if (!routing_walk_log(walk, "warn", "%s is rate limited, %s %s",
                          routing_walk_entry_member(walk, walk->index, "model"), what_follows(walk), walk->requested)) {
        return NULL;
    }
    return next_entry(walk, providers, walk->index + 1);
}

/** @implNote A failure that is not rate limiting ends the request with the provider's own answer, no fallback tried. */
static int32_t read_failure(step_walk *walk, const cJSON *providers, const cJSON *report, service_reply *reply) {
    int32_t status = SERVICE_SUCCESS;
    if (!read_failure_members(report, reply, &status)) {
        return status;
    }
    cJSON *outcome = routing_outcome_of(report);
    if (outcome == NULL) {
        return service_out_of_memory(reply);
    }
    const cJSON *headers = cJSON_GetObjectItemCaseSensitive(report, "headers");
    double reply_status = cJSON_GetObjectItemCaseSensitive(report, "status")->valuedouble;
    cJSON *answer = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(outcome, "rateLimited"))
                        ? rate_limited(walk, providers, headers, outcome)
                        : routing_answer_of(walk, routing_provider_failure_step(reply_status, headers), 0);
    cJSON_Delete(outcome);
    return service_answer(answer, reply);
}

static const report_reading report_readings[] = {
    {"response", read_served},
    {"streamed", read_served},
    {"streamFailed", read_stream_failed},
    {"thrown", read_thrown},
    {"failure", read_failure},
};

static report_reader reader_for(const char *kind) {
    for (size_t index = 0; index < sizeof(report_readings) / sizeof(report_readings[0]); index++) {
        if (strcmp(report_readings[index].kind, kind) == 0) {
            return report_readings[index].read;
        }
    }
    return NULL;
}

static int is_string_list(const cJSON *value) {
    if (!cJSON_IsArray(value)) {
        return 0;
    }
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, value) {
        if (!cJSON_IsString(item)) {
            return 0;
        }
    }
    return 1;
}

static int32_t continue_walk(const service_call *call, const cJSON *providers, service_reply *reply) {
    int32_t status = SERVICE_SUCCESS;
    const cJSON *report = NULL;
    const cJSON *kind = NULL;
    if (!service_require(call, "report", service_is_object, "an object", &report, reply, &status) ||
        !service_require_member(report, "the report member", "kind", service_is_string, "a string", &kind, reply,
                                &status)) {
        return status;
    }
    report_reader read = reader_for(kind->valuestring);
    step_walk walk = {.log = cJSON_CreateArray()};
    const cJSON *handed_back = cJSON_GetObjectItemCaseSensitive(call->argument, "walk");
    if (walk.log == NULL || !routing_walk_read(handed_back, &walk)) {
        status = service_out_of_memory(reply);
    } else {
        status = read(&walk, providers, report, reply);
    }
    routing_walk_discard(&walk);
    return status;
}

static char *login_hints(const cJSON *providers, const char *command) {
    char *hints = NULL;
    const cJSON *id = NULL;
    cJSON_ArrayForEach(id, providers) {
        char *longer = hints == NULL ? routing_formatted("%s %s login", command, id->valuestring)
                                     : routing_formatted("%s or %s %s login", hints, command, id->valuestring);
        spisor_memory_release(hints);
        if (longer == NULL) {
            return NULL;
        }
        hints = longer;
    }
    return hints == NULL ? routing_formatted("%s", "") : hints;
}

static cJSON *no_model_step(const step_walk *walk, const cJSON *providers, const cJSON *wording) {
    const char *command = cJSON_GetObjectItemCaseSensitive(wording, "loginCommand")->valuestring;
    const char *settings = cJSON_GetObjectItemCaseSensitive(wording, "settingsFile")->valuestring;
    char *hints = login_hints(providers, command);
    char *message = hints == NULL ? NULL
                                  : routing_formatted("no running provider offers a model for %s; log a provider in "
                                                      "with %s, or set routing.modelMap in %s",
                                                      walk->requested, hints, settings);
    cJSON *step = message == NULL ? NULL : routing_reply_step("apiError", SERVICE_UNAVAILABLE, message);
    spisor_memory_release(hints);
    spisor_memory_release(message);
    return step;
}

static int log_notices(step_walk *walk, const cJSON *notices) {
    const cJSON *notice = NULL;
    cJSON_ArrayForEach(notice, notices) {
        if (!routing_walk_log(walk, "info", "routing %s: %s", walk->requested, notice->valuestring)) {
            return 0;
        }
    }
    return 1;
}

/**
 * @implNote An empty chain is answered with a refresh first, since a login after the last refresh offers
 * models the snapshot lacks; only an empty chain after that refresh is refused.
 */
static cJSON *walk_over(step_walk *walk, const cJSON *resolution, const cJSON *providers, const service_call *call) {
    walk->chain = cJSON_GetObjectItemCaseSensitive(resolution, "chain");
    if (!log_notices(walk, cJSON_GetObjectItemCaseSensitive(resolution, "notices"))) {
        return NULL;
    }
    if (cJSON_GetArraySize(walk->chain) > 0) {
        return next_entry(walk, providers, 0);
    }
    if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(call->argument, "refreshed"))) {
        return routing_answer_of(walk, routing_kind_step("refreshModels"), 0);
    }
    const cJSON *wording = cJSON_GetObjectItemCaseSensitive(call->argument, "wording");
    return routing_answer_of(walk, no_model_step(walk, providers, wording), 0);
}

static int32_t routed(const service_call *call, const cJSON *providers, step_walk *walk, service_reply *reply) {
    int32_t status = SERVICE_SUCCESS;
    const cJSON *request = cJSON_GetObjectItemCaseSensitive(call->argument, "request");
    const cJSON *model = cJSON_GetObjectItemCaseSensitive(request, "model");
    const char *requested = model == NULL ? "" : model->valuestring;
    routing_profile profile;
    if (!routing_read_profile(cJSON_GetObjectItemCaseSensitive(call->argument, "profile"), ROUTING_RESOLUTION_NEEDS,
                              &profile, reply, &status)) {
        return status;
    }
    routing_refusal refusal = {0, "", NULL};
    cJSON *resolution = routing_resolution(&profile, cJSON_GetObjectItemCaseSensitive(call->argument, "map"),
                                           cJSON_GetObjectItemCaseSensitive(call->argument, "models"),
                                           requested, &refusal);
    if (resolution == NULL) {
        return routing_answer_refusal(&refusal, reply);
    }
    walk->requested = requested;
    status = service_answer(walk_over(walk, resolution, providers, call), reply);
    cJSON_Delete(resolution);
    return status;
}

/** @implNote With no provider running every request goes upstream undecoded, so it is never resolved. */
static int is_passed_through(const cJSON *request, const cJSON *providers) {
    return cJSON_GetArraySize(providers) == 0 ||
           cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(request, "passThrough"));
}

static int read_first_call(const service_call *call, service_reply *reply, int32_t *status) {
    const cJSON *member = NULL;
    const cJSON *wording = NULL;
    return service_require(call, "profile", service_is_object, "an object", &member, reply, status) &&
           service_require(call, "map", service_is_object, "an object", &member, reply, status) &&
           service_require(call, "models", service_is_object, "an object", &member, reply, status) &&
           service_require(call, "request", service_is_object, "an object", &member, reply, status) &&
           service_require(call, "wording", service_is_object, "an object", &wording, reply, status) &&
           service_require_member(wording, "the wording member", "loginCommand", service_is_string, "a string",
                                  &member, reply, status) &&
           service_require_member(wording, "the wording member", "settingsFile", service_is_string, "a string",
                                  &member, reply, status);
}

static int32_t start_walk(const service_call *call, const cJSON *providers, service_reply *reply) {
    int32_t status = SERVICE_SUCCESS;
    if (!read_first_call(call, reply, &status)) {
        return status;
    }
    step_walk walk = {.log = cJSON_CreateArray()};
    if (walk.log == NULL) {
        status = service_out_of_memory(reply);
    } else if (is_passed_through(cJSON_GetObjectItemCaseSensitive(call->argument, "request"), providers)) {
        status = service_answer(routing_answer_of(&walk, routing_kind_step("passThrough"), 0), reply);
    } else {
        status = routed(call, providers, &walk, reply);
    }
    routing_walk_discard(&walk);
    return status;
}

int32_t routing_step_call(const cJSON *argument, service_reply *reply) {
    service_call call = {argument};
    int32_t status = SERVICE_SUCCESS;
    const cJSON *providers = NULL;
    if (service_require(&call, "providers", is_string_list, "a list of strings", &providers, reply, &status)) {
        status = cJSON_GetObjectItemCaseSensitive(call.argument, "walk") == NULL
                     ? start_walk(&call, providers, reply)
                     : continue_walk(&call, providers, reply);
    }
    return status;
}
