#include "routing_failure.h"

#include "spisor/allocation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "routing_upstream.h"

#define BAD_GATEWAY 502
#define INTERNAL_ERROR 500

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

static spisor_status answer_unreachable(const cJSON *argument, const char *detail, service_reply *reply) {
    spisor_status status = SPISOR_OK;
    const cJSON *upstream = cJSON_GetObjectItemCaseSensitive(argument, "upstream");
    routing_upstream parts;
    if (routing_read_upstream(upstream->valuestring, &parts, reply, &status)) {
        status = service_answer(unreachable_answer(parts.origin, detail), reply);
        routing_free_upstream(&parts);
    }
    return status;
}

spisor_status routing_failure_call(const cJSON *argument, service_reply *reply) {
    const char *reason = cJSON_GetObjectItemCaseSensitive(argument, "reason")->valuestring;
    const char *detail = cJSON_GetObjectItemCaseSensitive(argument, "detail")->valuestring;
    return strcmp(reason, "upstreamUnreachable") == 0 ? answer_unreachable(argument, detail, reply)
                                                      : service_answer(request_failed_answer(detail), reply);
}
