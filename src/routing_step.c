#include "routing_step.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "routing_resolve.h"
#include "routing_step_answer.h"

#define BAD_GATEWAY 502
#define SERVICE_UNAVAILABLE 503
#define REPORT_KIND_FIX "report response, streamed, streamFailed or thrown"

typedef int32_t (*report_reader)(step_walk *walk, const cJSON *providers, const cJSON *report, uint8_t **out,
                                 size_t *out_len);

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
    char *message = routing_formatted("no chain entry could serve the request: %s", walk->failure == NULL ? "" : walk->failure);
    cJSON *step = message == NULL ? NULL : routing_reply_step("proxyError", BAD_GATEWAY, message);
    free(message);
    return routing_answer_of(walk, step, 0);
}

static int skip_missing(step_walk *walk, int index) {
    return routing_walk_remember_failure(walk, routing_formatted("no running plugin provides %s, which the model map names for %s",
                                            routing_walk_entry_member(walk, index, "provider"), routing_walk_entry_member(walk, index, "model"))) &&
           routing_walk_log(walk, "warn", "%s", walk->failure);
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
    if (walk->index > 0 && !routing_walk_log(walk, "warn", "%s served %s in place of %s", routing_walk_entry_member(walk, walk->index, "model"),
                                     walk->requested, routing_walk_entry_member(walk, 0, "model"))) {
        cJSON_Delete(step);
        return NULL;
    }
    return routing_answer_of(walk, step, 0);
}

static int require_member(const cJSON *object, const char *subject, const char *name, int (*is_type)(const cJSON *),
                          const char *type_name, const cJSON **member, uint8_t **out, size_t *out_len,
                          int32_t *status) {
    *member = cJSON_GetObjectItemCaseSensitive(object, name);
    if (*member != NULL && is_type(*member)) {
        return 1;
    }
    char problem[128];
    char fix[128];
    snprintf(problem, sizeof(problem), "%s is missing or is not %s", name, type_name);
    snprintf(fix, sizeof(fix), "send %s as %s", name, type_name);
    *status = service_fail(subject, problem, fix, out, out_len);
    return 0;
}

static int32_t read_served(step_walk *walk, const cJSON *providers, const cJSON *report, uint8_t **out,
                           size_t *out_len) {
    (void)providers;
    (void)report;
    return service_answer(served(walk, routing_kind_step("done")), out, out_len);
}

static int32_t read_thrown(step_walk *walk, const cJSON *providers, const cJSON *report, uint8_t **out,
                           size_t *out_len) {
    int32_t status = SPISOR_STATUS_SUCCESS;
    const cJSON *message = NULL;
    if (!require_member(report, "the report member", "message", service_is_string, "a string", &message, out, out_len,
                        &status)) {
        return status;
    }
    cJSON *answer = NULL;
    if (routing_walk_remember_failure(walk, routing_formatted("%s", message->valuestring)) &&
        routing_walk_log(walk, "warn", "%s failed %s, %s %s: %s", routing_walk_entry_member(walk, walk->index, "provider"),
                 routing_walk_entry_member(walk, walk->index, "model"), what_follows(walk), walk->requested, message->valuestring)) {
        answer = next_entry(walk, providers, walk->index + 1);
    }
    return service_answer(answer, out, out_len);
}

static cJSON *stream_failed_before_bytes(step_walk *walk, const cJSON *providers, const char *message) {
    if (!routing_walk_remember_failure(walk, routing_formatted("%s", message)) ||
        !routing_walk_log(walk, "warn", "the stream of %s failed on its first event, %s %s: %s",
                  routing_walk_entry_member(walk, walk->index, "model"), what_follows(walk), walk->requested, message)) {
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

static int32_t read_stream_failed(step_walk *walk, const cJSON *providers, const cJSON *report, uint8_t **out,
                                  size_t *out_len) {
    int32_t status = SPISOR_STATUS_SUCCESS;
    const cJSON *after_bytes = NULL;
    const cJSON *message = NULL;
    if (!require_member(report, "the report member", "afterBytes", service_is_boolean, "a boolean", &after_bytes, out,
                        out_len, &status) ||
        !require_member(report, "the report member", "message", service_is_string, "a string", &message, out, out_len,
                        &status)) {
        return status;
    }
    cJSON *answer = cJSON_IsTrue(after_bytes) ? stream_failed_after_bytes(walk, message->valuestring)
                                              : stream_failed_before_bytes(walk, providers, message->valuestring);
    return service_answer(answer, out, out_len);
}

static const report_reading report_readings[] = {
    {"response", read_served},
    {"streamed", read_served},
    {"streamFailed", read_stream_failed},
    {"thrown", read_thrown},
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

static int32_t continue_walk(const spisor_call *call, const cJSON *providers, uint8_t **out, size_t *out_len) {
    int32_t status = SPISOR_STATUS_SUCCESS;
    const cJSON *report = NULL;
    const cJSON *kind = NULL;
    if (!service_require(call, "report", service_is_object, "an object", &report, out, out_len, &status) ||
        !require_member(report, "the report member", "kind", service_is_string, "a string", &kind, out, out_len,
                        &status)) {
        return status;
    }
    report_reader read = reader_for(kind->valuestring);
    if (read == NULL) {
        char cause[160];
        snprintf(cause, sizeof(cause), "the report kind %.64s is not one routing.step reads", kind->valuestring);
        return service_refuse(cause, REPORT_KIND_FIX, out, out_len);
    }
    step_walk walk = {NULL, NULL, 0, NULL, cJSON_CreateArray()};
    routing_refusal refusal = {0, "", NULL};
    if (walk.log == NULL || !routing_walk_read(cJSON_GetObjectItemCaseSensitive(call->argument, "walk"), &walk, &refusal)) {
        status = routing_answer_refusal(&refusal, out, out_len);
    } else {
        status = read(&walk, providers, report, out, out_len);
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
        free(hints);
        if (longer == NULL) {
            return NULL;
        }
        hints = longer;
    }
    return hints;
}

static cJSON *no_model_step(const step_walk *walk, const cJSON *providers, const cJSON *wording) {
    const char *command = cJSON_GetObjectItemCaseSensitive(wording, "loginCommand")->valuestring;
    const char *settings = cJSON_GetObjectItemCaseSensitive(wording, "settingsFile")->valuestring;
    char *hints = login_hints(providers, command);
    char *message = hints == NULL ? NULL
                                  : routing_formatted("no running provider offers a model for %s; log a provider in with %s, "
                                              "or set routing.modelMap in %s",
                                              walk->requested, hints, settings);
    cJSON *step = message == NULL ? NULL : routing_reply_step("apiError", SERVICE_UNAVAILABLE, message);
    free(hints);
    free(message);
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
static cJSON *walk_over(step_walk *walk, const cJSON *resolution, const cJSON *providers, const spisor_call *call) {
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
    return routing_answer_of(walk, no_model_step(walk, providers, cJSON_GetObjectItemCaseSensitive(call->argument, "wording")), 0);
}

static int32_t routed(const spisor_call *call, const cJSON *providers, step_walk *walk, uint8_t **out,
                      size_t *out_len) {
    int32_t status = SPISOR_STATUS_SUCCESS;
    const cJSON *request = cJSON_GetObjectItemCaseSensitive(call->argument, "request");
    const cJSON *model = NULL;
    routing_profile profile;
    if (!require_member(request, "the request member", "model", service_is_string, "a string", &model, out, out_len,
                        &status) ||
        !routing_read_profile(cJSON_GetObjectItemCaseSensitive(call->argument, "profile"), ROUTING_RESOLUTION_NEEDS,
                              &profile, out, out_len, &status)) {
        return status;
    }
    routing_refusal refusal = {0, "", NULL};
    cJSON *resolution = routing_resolution(&profile, cJSON_GetObjectItemCaseSensitive(call->argument, "map"),
                                           cJSON_GetObjectItemCaseSensitive(call->argument, "models"),
                                           model->valuestring, &refusal);
    if (resolution == NULL) {
        return routing_answer_refusal(&refusal, out, out_len);
    }
    walk->requested = model->valuestring;
    status = service_answer(walk_over(walk, resolution, providers, call), out, out_len);
    cJSON_Delete(resolution);
    return status;
}

/** @implNote With no provider running every request goes upstream undecoded, so it is never resolved. */
static int is_passed_through(const cJSON *request, const cJSON *providers) {
    return cJSON_GetArraySize(providers) == 0 ||
           cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(request, "passThrough"));
}

static int read_first_call(const spisor_call *call, uint8_t **out, size_t *out_len, int32_t *status) {
    const cJSON *member = NULL;
    const cJSON *wording = NULL;
    return service_require(call, "profile", service_is_object, "an object", &member, out, out_len, status) &&
           service_require(call, "map", service_is_object, "an object", &member, out, out_len, status) &&
           service_require(call, "models", service_is_object, "an object", &member, out, out_len, status) &&
           service_require(call, "request", service_is_object, "an object", &member, out, out_len, status) &&
           service_require(call, "wording", service_is_object, "an object", &wording, out, out_len, status) &&
           require_member(wording, "the wording member", "loginCommand", service_is_string, "a string", &member, out,
                          out_len, status) &&
           require_member(wording, "the wording member", "settingsFile", service_is_string, "a string", &member, out,
                          out_len, status);
}

static int32_t start_walk(const spisor_call *call, const cJSON *providers, uint8_t **out, size_t *out_len) {
    int32_t status = SPISOR_STATUS_SUCCESS;
    if (!read_first_call(call, out, out_len, &status)) {
        return status;
    }
    step_walk walk = {NULL, NULL, 0, NULL, cJSON_CreateArray()};
    if (walk.log == NULL) {
        status = service_out_of_memory(out, out_len);
    } else if (is_passed_through(cJSON_GetObjectItemCaseSensitive(call->argument, "request"), providers)) {
        status = service_answer(routing_answer_of(&walk, routing_kind_step("passThrough"), 0), out, out_len);
    } else {
        status = routed(call, providers, &walk, out, out_len);
    }
    routing_walk_discard(&walk);
    return status;
}

SERVICE_EXPORT int32_t forebay_library_routing_step(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len) {
    spisor_call call;
    int32_t status = service_open(in, in_len, &call, out, out_len);
    if (status != SPISOR_STATUS_SUCCESS) {
        return status;
    }
    const cJSON *providers = NULL;
    if (service_require(&call, "providers", is_string_list, "a list of strings", &providers, out, out_len, &status)) {
        status = cJSON_GetObjectItemCaseSensitive(call.argument, "walk") == NULL
                     ? start_walk(&call, providers, out, out_len)
                     : continue_walk(&call, providers, out, out_len);
    }
    spisor_discard_call(&call);
    return status;
}
