#include "routing_failure.h"

#include "spisor/allocation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "routing_upstream.h"

#define BAD_GATEWAY 502
#define INTERNAL_ERROR 500
#define REASON_FIX "pass reason as upstreamUnreachable or requestFailed"

static cJSON *proxy_error(int status, const char *message) {
    cJSON *reply = cJSON_CreateObject();
    if (reply == NULL || cJSON_AddStringToObject(reply, "kind", "proxyError") == NULL ||
        cJSON_AddNumberToObject(reply, "status", status) == NULL ||
        cJSON_AddStringToObject(reply, "message", message) == NULL) {
        cJSON_Delete(reply);
        return NULL;
    }
    return reply;
}

static cJSON *warning_log(const char *message) {
    cJSON *log = cJSON_CreateArray();
    cJSON *line = cJSON_CreateObject();
    if (log == NULL || line == NULL || cJSON_AddStringToObject(line, "level", "warn") == NULL ||
        cJSON_AddStringToObject(line, "message", message) == NULL) {
        cJSON_Delete(line);
        cJSON_Delete(log);
        return NULL;
    }
    if (!service_push(log, line)) {
        cJSON_Delete(log);
        return NULL;
    }
    return log;
}

static cJSON *unreachable_answer(const char *origin, const char *detail) {
    char *message = routing_formatted("the upstream %s could not be reached: %s", origin, detail);
    cJSON *answer = message == NULL ? NULL : service_wrap("reply", proxy_error(BAD_GATEWAY, message));
    spisor_memory_release(message);
    return answer;
}

static cJSON *request_failed_answer(const char *detail) {
    char *warning = routing_formatted("a request failed: %s", detail);
    cJSON *answer = warning == NULL ? NULL : service_wrap("reply", proxy_error(INTERNAL_ERROR, detail));
    if (answer != NULL && !service_add(answer, "log", warning_log(warning))) {
        cJSON_Delete(answer);
        answer = NULL;
    }
    spisor_memory_release(warning);
    return answer;
}

static int32_t answer_unreachable(const service_call *call, const char *detail, service_reply *reply) {
    int32_t status = SERVICE_SUCCESS;
    const cJSON *upstream = NULL;
    routing_upstream parts;
    if (service_require(call, "upstream", service_is_string, "a string", &upstream, reply, &status) &&
        routing_read_upstream(upstream->valuestring, &parts, reply, &status)) {
        status = service_answer(unreachable_answer(parts.origin, detail), reply);
        routing_free_upstream(&parts);
    }
    return status;
}

static int32_t refuse_reason(const char *reason, service_reply *reply) {
    char cause[160];
    snprintf(cause, sizeof(cause), "the reason %.64s is not one routing.failure words", reason);
    return service_refuse(cause, REASON_FIX, reply);
}

static int32_t answer_failure(const service_call *call, const char *reason, const char *detail, service_reply *reply) {
    if (strcmp(reason, "upstreamUnreachable") == 0) {
        return answer_unreachable(call, detail, reply);
    }
    if (strcmp(reason, "requestFailed") == 0) {
        return service_answer(request_failed_answer(detail), reply);
    }
    return refuse_reason(reason, reply);
}

int32_t routing_failure_call(const cJSON *argument, service_reply *reply) {
    service_call call = {argument};
    int32_t status = SERVICE_SUCCESS;
    const cJSON *reason = NULL;
    const cJSON *detail = NULL;
    if (service_require(&call, "reason", service_is_string, "a string", &reason, reply, &status) &&
        service_require(&call, "detail", service_is_string, "a string", &detail, reply, &status)) {
        status = answer_failure(&call, reason->valuestring, detail->valuestring, reply);
    }
    return status;
}
