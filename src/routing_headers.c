#include "routing_headers.h"

#include "spisor/allocation.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "routing_upstream.h"

#define PROXY_PREFIX "proxy-"
#define URL_FIX "pass the request's path and query as url, such as /v1/messages"

static const char *const HOP_BY_HOP[] = {"connection", "keep-alive", "proxy-connection", "transfer-encoding",
                                         "upgrade",    "te",         "trailer"};
static const char *const REFRAMED[] = {"content-encoding", "content-length"};

static int is_one_of(const char *name, const char *const *names, size_t count) {
    for (size_t index = 0; index < count; index++) {
        if (routing_equals_ignoring_case(name, names[index])) {
            return 1;
        }
    }
    return 0;
}

static int is_hop_by_hop(const char *name) {
    return is_one_of(name, HOP_BY_HOP, sizeof(HOP_BY_HOP) / sizeof(HOP_BY_HOP[0])) ||
           routing_starts_with_ignoring_case(name, PROXY_PREFIX);
}

static int is_reframed(const char *name) {
    return is_one_of(name, REFRAMED, sizeof(REFRAMED) / sizeof(REFRAMED[0]));
}

static int token_names(const char *start, const char *end, const char *name) {
    for (; start < end && *name != '\0'; start++, name++) {
        if (tolower((unsigned char)*start) != tolower((unsigned char)*name)) {
            return 0;
        }
    }
    return start == end && *name == '\0';
}

static int is_listed_in_connection(const char *connection, const char *name) {
    while (connection != NULL && *connection != '\0') {
        size_t token_length = strcspn(connection, ",");
        const char *start = connection;
        const char *end = connection + token_length;
        while (start < end && isspace((unsigned char)*start)) {
            start++;
        }
        while (end > start && isspace((unsigned char)end[-1])) {
            end--;
        }
        if (start < end && token_names(start, end, name)) {
            return 1;
        }
        connection += token_length;
        if (*connection == ',') {
            connection++;
        }
    }
    return 0;
}

static int is_dropped(const char *name, const char *connection, int rewritten) {
    return is_hop_by_hop(name) || is_listed_in_connection(connection, name) || (rewritten && is_reframed(name));
}

cJSON *routing_end_to_end_headers(const cJSON *headers, int rewritten) {
    cJSON *kept = cJSON_CreateObject();
    if (kept == NULL) {
        return NULL;
    }
    const char *connection = routing_header(headers, "connection");
    const cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, headers) {
        if (cJSON_IsString(entry) && !is_dropped(entry->string, connection, rewritten) &&
            cJSON_AddStringToObject(kept, entry->string, entry->valuestring) == NULL) {
            cJSON_Delete(kept);
            return NULL;
        }
    }
    return kept;
}

static int is_set_by_the_proxy(const char *name) {
    return routing_equals_ignoring_case(name, "host") || routing_equals_ignoring_case(name, "content-length");
}

static int is_placeholder_authorization(const cJSON *entry, const char *placeholder) {
    return routing_equals_ignoring_case(entry->string, "authorization") && strcmp(entry->valuestring, placeholder) == 0;
}

static void drop_what_the_proxy_replaces(cJSON *headers, const char *placeholder) {
    cJSON *entry = headers->child;
    while (entry != NULL) {
        cJSON *next = entry->next;
        if (is_set_by_the_proxy(entry->string) || is_placeholder_authorization(entry, placeholder)) {
            cJSON_Delete(cJSON_DetachItemViaPointer(headers, entry));
        }
        entry = next;
    }
}

static cJSON *forwarded_headers(const cJSON *headers, const char *placeholder, const char *host, double body_length) {
    cJSON *forwarded = routing_end_to_end_headers(headers, 0);
    if (forwarded == NULL) {
        return NULL;
    }
    drop_what_the_proxy_replaces(forwarded, placeholder);
    char length[32];
    snprintf(length, sizeof(length), "%.0f", body_length);
    if (cJSON_AddStringToObject(forwarded, "host", host) == NULL ||
        cJSON_AddStringToObject(forwarded, "content-length", length) == NULL) {
        cJSON_Delete(forwarded);
        return NULL;
    }
    return forwarded;
}

static char *join_target(const char *base, const char *url) {
    size_t base_length = strlen(base);
    char *target = spisor_memory_allocate(base_length + strlen(url) + 1);
    if (target != NULL) {
        memcpy(target, base, base_length);
        strcpy(target + base_length, url);
    }
    return target;
}

static cJSON *pass_through_answer(const routing_upstream *parts, const cJSON *url, const cJSON *headers,
                                  const cJSON *placeholder, double body_length) {
    cJSON *result = cJSON_CreateObject();
    char *target = join_target(parts->base, url->valuestring);
    cJSON *forwarded = forwarded_headers(headers, placeholder->valuestring, parts->host, body_length);
    if (result == NULL || target == NULL || forwarded == NULL ||
        cJSON_AddStringToObject(result, "target", target) == NULL ||
        !cJSON_AddItemToObject(result, "headers", forwarded)) {
        cJSON_Delete(forwarded);
        cJSON_Delete(result);
        result = NULL;
    }
    spisor_memory_release(target);
    return result;
}

typedef struct pass_through_arguments {
    const cJSON *upstream;
    const cJSON *url;
    const cJSON *headers;
    const cJSON *body_length;
    const cJSON *placeholder;
} pass_through_arguments;

static pass_through_arguments read_pass_through_arguments(const cJSON *argument) {
    pass_through_arguments read = {
        cJSON_GetObjectItemCaseSensitive(argument, "upstream"),
        cJSON_GetObjectItemCaseSensitive(argument, "url"),
        cJSON_GetObjectItemCaseSensitive(argument, "headers"),
        cJSON_GetObjectItemCaseSensitive(argument, "bodyLength"),
        cJSON_GetObjectItemCaseSensitive(argument, "placeholderAuthorization"),
    };
    return read;
}

static int32_t refuse_url(const char *url, service_reply *reply) {
    char cause[512];
    snprintf(cause, sizeof(cause), "the url %.400s is not a path starting with /", url);
    return service_refuse(cause, URL_FIX, reply);
}

/** @implNote A url not starting with `/` would join onto the upstream's host, so it could name another host. */
static int32_t answer_pass_through(const pass_through_arguments *arguments, service_reply *reply) {
    if (arguments->url->valuestring[0] != '/') {
        return refuse_url(arguments->url->valuestring, reply);
    }
    routing_upstream parts;
    int32_t status = SERVICE_SUCCESS;
    if (!routing_read_upstream(arguments->upstream->valuestring, &parts, reply, &status)) {
        return status;
    }
    status = service_answer(pass_through_answer(&parts, arguments->url, arguments->headers, arguments->placeholder,
                                                arguments->body_length->valuedouble),
                            reply);
    routing_free_upstream(&parts);
    return status;
}

int32_t routing_pass_through_call(const cJSON *argument, service_reply *reply) {
    pass_through_arguments arguments = read_pass_through_arguments(argument);
    return answer_pass_through(&arguments, reply);
}

int32_t routing_reply_headers_call(const cJSON *argument, service_reply *reply) {
    const cJSON *headers = cJSON_GetObjectItemCaseSensitive(argument, "headers");
    int rewritten = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(argument, "rewritten"));
    return service_answer(service_wrap("headers", routing_end_to_end_headers(headers, rewritten)), reply);
}
