#include "routing_arrival.h"

#include "answer_storage.h"
#include "spisor/allocation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FORBIDDEN_STATUS 403u
#define HEALTH_STATUS 200u
#define HEALTH_PATH "/health"
#define LOOPBACK_ADDRESS "127.0.0.1"
#define LOOPBACK_NAME "localhost"

/** The value of the header the JavaScript Headers API reads: the exact name first, else the first ignoring case. */
static const char *header_value(const forebay_pair *headers, size_t count, const char *name) {
    const char *exact = NULL;
    for (size_t index = 0; index < count; index++) {
        if (strcmp(headers[index].name, name) == 0) {
            exact = headers[index].value;
        }
    }
    for (size_t index = 0; exact == NULL && index < count; index++) {
        if (routing_equals_ignoring_case(headers[index].name, name)) {
            return header_value(headers, count, headers[index].name);
        }
    }
    return exact;
}

static int is_addressed_to_the_port(const forebay_pair *headers, size_t count, uint32_t port) {
    const char *host = header_value(headers, count, "host");
    char address[32];
    char name[32];
    snprintf(address, sizeof address, LOOPBACK_ADDRESS ":%u", (unsigned)port);
    snprintf(name, sizeof name, LOOPBACK_NAME ":%u", (unsigned)port);
    return host != NULL && (routing_equals_ignoring_case(host, address) || routing_equals_ignoring_case(host, name));
}

static int carries_an_origin(const forebay_pair *headers, size_t count) {
    for (size_t index = 0; index < count; index++) {
        if (routing_equals_ignoring_case(headers[index].name, "origin")) {
            return 1;
        }
    }
    return 0;
}

static int is_health_probe(const char *path) {
    size_t length = strcspn(path, "?");
    return length == strlen(HEALTH_PATH) && strncmp(path, HEALTH_PATH, length) == 0;
}

static spisor_status refuse_arrival(uint32_t port, forebay_arrival_answer *answer) {
    char message[160];
    snprintf(message, sizeof message,
             "the proxy answers only requests to " LOOPBACK_ADDRESS ":%u or " LOOPBACK_NAME ":%u that carry no Origin",
             (unsigned)port, (unsigned)port);
    if (!forebay_storage_text(&answer->storage, message, &answer->message)) {
        return SPISOR_OUT_OF_MEMORY;
    }
    answer->kind = FOREBAY_ARRIVAL_REFUSE;
    answer->reply_kind = FOREBAY_REPLY_PROXY_ERROR;
    answer->status = FORBIDDEN_STATUS;
    return SPISOR_OK;
}

spisor_status routing_decide_arrival(const forebay_arrival_argument *argument, forebay_arrival_answer *answer) {
    if (carries_an_origin(argument->headers, argument->header_count) ||
        !is_addressed_to_the_port(argument->headers, argument->header_count, argument->port)) {
        return refuse_arrival(argument->port, answer);
    }
    if (is_health_probe(argument->path)) {
        answer->kind = FOREBAY_ARRIVAL_HEALTH;
        answer->status = HEALTH_STATUS;
        answer->content_type = "text/plain";
        answer->body = "ok";
    } else {
        answer->kind = FOREBAY_ARRIVAL_ROUTE;
    }
    return SPISOR_OK;
}

static spisor_status refuse_the_setting(const cJSON *value, const char *settings_file, service_reply *reply) {
    char *printed = cJSON_PrintUnformatted(value);
    char *cause = printed == NULL ? NULL : routing_formatted("proxy.port is %s, which is no port", printed);
    char *fix = routing_formatted("set proxy.port in %s to a port from 1 to 65535", settings_file);
    spisor_status status =
        cause == NULL || fix == NULL ? service_out_of_memory(reply) : service_refuse(cause, fix, reply);
    spisor_memory_release(printed);
    spisor_memory_release(cause);
    spisor_memory_release(fix);
    return status;
}

static spisor_status refuse_the_listen(int port, const cJSON *failure, const char *settings_file,
                                       service_reply *reply) {
    const char *detail = cJSON_GetObjectItemCaseSensitive(failure, "detail")->valuestring;
    int in_use = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(failure, "inUse"));
    char *reason =
        in_use ? routing_formatted("is already in use") : routing_formatted("cannot be listened on (%s)", detail);
    if (reason == NULL) {
        return service_out_of_memory(reply);
    }
    char *cause = routing_formatted("port %d on " LOOPBACK_ADDRESS " %s, so the proxy cannot listen", port, reason);
    char *fix =
        routing_formatted("stop what listens on port %d, or set proxy.port in %s to a free port", port, settings_file);
    spisor_status status =
        cause == NULL || fix == NULL ? service_out_of_memory(reply) : service_refuse(cause, fix, reply);
    spisor_memory_release(reason);
    spisor_memory_release(cause);
    spisor_memory_release(fix);
    return status;
}

static spisor_status answer_the_port(int port, service_reply *reply) {
    cJSON *answer = cJSON_CreateObject();
    if (answer == NULL || cJSON_AddNumberToObject(answer, "port", port) == NULL ||
        cJSON_AddStringToObject(answer, "address", LOOPBACK_ADDRESS) == NULL) {
        cJSON_Delete(answer);
        answer = NULL;
    }
    return service_answer(answer, reply);
}

static spisor_status decide_the_port(const cJSON *value, const char *settings_file, const cJSON *failure,
                                     service_reply *reply) {
    if (!routing_is_port(value)) {
        return refuse_the_setting(value, settings_file, reply);
    }
    if (failure != NULL) {
        return refuse_the_listen((int)value->valuedouble, failure, settings_file, reply);
    }
    return answer_the_port((int)value->valuedouble, reply);
}

spisor_status routing_port_call(const cJSON *argument, service_reply *reply) {
    return decide_the_port(cJSON_GetObjectItemCaseSensitive(argument, "value"),
                           cJSON_GetObjectItemCaseSensitive(argument, "settingsFile")->valuestring,
                           cJSON_GetObjectItemCaseSensitive(argument, "failure"), reply);
}
