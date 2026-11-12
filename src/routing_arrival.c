#include "routing_arrival.h"

#include <stdlib.h>
#include <string.h>

#define FORBIDDEN_STATUS 403
#define HEALTH_PATH "/health"
#define LOOPBACK_ADDRESS "127.0.0.1"
#define LOOPBACK_NAME "localhost"

static int is_addressed_to_the_port(const cJSON *headers, int port) {
    const char *host = routing_header(headers, "host");
    char *address = routing_formatted(LOOPBACK_ADDRESS ":%d", port);
    char *name = routing_formatted(LOOPBACK_NAME ":%d", port);
    int admitted = host != NULL && address != NULL && name != NULL &&
                   (routing_equals_ignoring_case(host, address) || routing_equals_ignoring_case(host, name));
    free(address);
    free(name);
    return admitted;
}

/** @implNote Any value counts, since a header the caller could not read as a string was still sent. */
static int carries_an_origin(const cJSON *headers) {
    const cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, headers) {
        if (routing_equals_ignoring_case(entry->string, "origin")) {
            return 1;
        }
    }
    return 0;
}

static int is_admitted(const cJSON *headers, int port) {
    return !carries_an_origin(headers) && is_addressed_to_the_port(headers, port);
}

static int is_health_probe(const char *path) {
    size_t length = strcspn(path, "?");
    return length == strlen(HEALTH_PATH) && strncmp(path, HEALTH_PATH, length) == 0;
}

static cJSON *refusal_answer(int port) {
    char *message = routing_formatted("the proxy answers only requests to " LOOPBACK_ADDRESS ":%d or " LOOPBACK_NAME
                                ":%d that carry no Origin",
                                port, port);
    cJSON *answer = cJSON_CreateObject();
    cJSON *reply = cJSON_CreateObject();
    if (message == NULL || answer == NULL || reply == NULL || cJSON_AddStringToObject(answer, "kind", "refuse") == NULL ||
        cJSON_AddStringToObject(reply, "kind", "proxyError") == NULL ||
        cJSON_AddNumberToObject(reply, "status", FORBIDDEN_STATUS) == NULL ||
        cJSON_AddStringToObject(reply, "message", message) == NULL || !service_add(answer, "reply", reply)) {
        cJSON_Delete(reply);
        cJSON_Delete(answer);
        answer = NULL;
    }
    free(message);
    return answer;
}

static cJSON *health_answer(void) {
    cJSON *answer = cJSON_CreateObject();
    if (answer == NULL || cJSON_AddStringToObject(answer, "kind", "health") == NULL ||
        cJSON_AddNumberToObject(answer, "status", 200) == NULL ||
        cJSON_AddStringToObject(answer, "contentType", "text/plain") == NULL ||
        cJSON_AddStringToObject(answer, "body", "ok") == NULL) {
        cJSON_Delete(answer);
        return NULL;
    }
    return answer;
}

static cJSON *route_answer(void) {
    cJSON *answer = cJSON_CreateObject();
    if (answer == NULL || cJSON_AddStringToObject(answer, "kind", "route") == NULL) {
        cJSON_Delete(answer);
        return NULL;
    }
    return answer;
}

static cJSON *arrival_answer(const char *path, const cJSON *headers, int port) {
    if (!is_admitted(headers, port)) {
        return refusal_answer(port);
    }
    return is_health_probe(path) ? health_answer() : route_answer();
}

SERVICE_EXPORT int32_t forebay_library_routing_arrival(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len) {
    spisor_call call;
    int32_t status = service_open(in, in_len, &call, out, out_len);
    if (status != SPISOR_STATUS_SUCCESS) {
        return status;
    }
    const cJSON *path = NULL;
    const cJSON *headers = NULL;
    const cJSON *port = NULL;
    if (service_require(&call, "path", service_is_string, "a string", &path, out, out_len, &status) &&
        service_require(&call, "headers", service_is_object, "an object", &headers, out, out_len, &status) &&
        service_require(&call, "port", routing_is_port, "a port number from 1 to 65535", &port, out, out_len, &status)) {
        status = service_answer(arrival_answer(path->valuestring, headers, (int)port->valuedouble), out, out_len);
    }
    spisor_discard_call(&call);
    return status;
}

static int is_listen_failure(const cJSON *value) {
    return cJSON_IsObject(value) && cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(value, "inUse")) &&
           cJSON_IsString(cJSON_GetObjectItemCaseSensitive(value, "detail"));
}

static int is_anything(const cJSON *value) {
    return value != NULL;
}

static int32_t refuse_the_setting(const cJSON *value, const char *settings_file, uint8_t **out, size_t *out_len) {
    char *printed = cJSON_PrintUnformatted(value);
    char *cause = printed == NULL ? NULL : routing_formatted("proxy.port is %s, which is no port", printed);
    char *fix = routing_formatted("set proxy.port in %s to a port from 1 to 65535", settings_file);
    int32_t status = cause == NULL || fix == NULL ? service_out_of_memory(out, out_len) : service_refuse(cause, fix, out, out_len);
    free(printed);
    free(cause);
    free(fix);
    return status;
}

static int32_t refuse_the_listen(int port, const cJSON *failure, const char *settings_file, uint8_t **out, size_t *out_len) {
    const char *detail = cJSON_GetObjectItemCaseSensitive(failure, "detail")->valuestring;
    int in_use = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(failure, "inUse"));
    char *reason = in_use ? routing_formatted("is already in use") : routing_formatted("cannot be listened on (%s)", detail);
    if (reason == NULL) {
        return service_out_of_memory(out, out_len);
    }
    char *cause = routing_formatted("port %d on " LOOPBACK_ADDRESS " %s, so the proxy cannot listen", port, reason);
    char *fix = routing_formatted("stop what listens on port %d, or set proxy.port in %s to a free port", port, settings_file);
    int32_t status = cause == NULL || fix == NULL ? service_out_of_memory(out, out_len) : service_refuse(cause, fix, out, out_len);
    free(reason);
    free(cause);
    free(fix);
    return status;
}

static int32_t answer_the_port(int port, uint8_t **out, size_t *out_len) {
    cJSON *answer = cJSON_CreateObject();
    if (answer == NULL || cJSON_AddNumberToObject(answer, "port", port) == NULL ||
        cJSON_AddStringToObject(answer, "address", LOOPBACK_ADDRESS) == NULL) {
        cJSON_Delete(answer);
        answer = NULL;
    }
    return service_answer(answer, out, out_len);
}

static int32_t decide_the_port(const cJSON *value, const char *settings_file, const cJSON *failure, uint8_t **out,
                               size_t *out_len) {
    if (!routing_is_port(value)) {
        return refuse_the_setting(value, settings_file, out, out_len);
    }
    if (failure != NULL) {
        return refuse_the_listen((int)value->valuedouble, failure, settings_file, out, out_len);
    }
    return answer_the_port((int)value->valuedouble, out, out_len);
}

SERVICE_EXPORT int32_t forebay_library_routing_port(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len) {
    spisor_call call;
    int32_t status = service_open(in, in_len, &call, out, out_len);
    if (status != SPISOR_STATUS_SUCCESS) {
        return status;
    }
    const cJSON *value = NULL;
    const cJSON *settings_file = NULL;
    const cJSON *failure = cJSON_GetObjectItemCaseSensitive(call.argument, "failure");
    if (failure != NULL && !is_listen_failure(failure)) {
        status = service_fail("argument", "member failure is not an object with inUse and detail",
                              "pass failure as {inUse: boolean, detail: string}", out, out_len);
    } else if (service_require(&call, "value", is_anything, "any value", &value, out, out_len, &status) &&
               service_require(&call, "settingsFile", service_is_string, "a string", &settings_file, out, out_len,
                               &status)) {
        status = decide_the_port(value, settings_file->valuestring, failure, out, out_len);
    }
    spisor_discard_call(&call);
    return status;
}
