#include "routing_headers.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROXY_PREFIX "proxy-"
#define SCHEME_HTTP "http://"
#define SCHEME_HTTPS "https://"
#define REFUSAL_FIX "give the profile an upstream such as https://api.example.com"

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

typedef struct upstream_parts {
    char *base;
    char *host;
} upstream_parts;

typedef enum upstream_parse {
    UPSTREAM_PARSED,
    UPSTREAM_REFUSED,
    UPSTREAM_OUT_OF_MEMORY
} upstream_parse;

static size_t scheme_length(const char *upstream) {
    if (routing_starts_with_ignoring_case(upstream, SCHEME_HTTP)) {
        return strlen(SCHEME_HTTP);
    }
    if (routing_starts_with_ignoring_case(upstream, SCHEME_HTTPS)) {
        return strlen(SCHEME_HTTPS);
    }
    return 0;
}

static size_t trimmed_path_length(const char *path) {
    size_t length = strcspn(path, "?#");
    while (length > 0 && path[length - 1] == '/') {
        length--;
    }
    return length;
}

static void lower_case(char *text, size_t length) {
    for (size_t index = 0; index < length; index++) {
        text[index] = (char)tolower((unsigned char)text[index]);
    }
}

static void free_parts(upstream_parts *parts) {
    free(parts->base);
    free(parts->host);
    parts->base = NULL;
    parts->host = NULL;
}

/**
 * Splits an absolute http(s) URL into the base a request path is appended to (scheme, host and port,
 * and the path without its trailing slashes) and the Host header value. The scheme and host are
 * lower-cased, as the URL parser the archived host used does.
 */
static upstream_parse split_upstream(const char *upstream, upstream_parts *parts) {
    parts->base = NULL;
    parts->host = NULL;
    size_t scheme = scheme_length(upstream);
    const char *authority = upstream + scheme;
    size_t authority_length = strcspn(authority, "/?#");
    if (scheme == 0 || authority_length == 0 || memchr(authority, '@', authority_length) != NULL) {
        return UPSTREAM_REFUSED;
    }
    size_t path_length = trimmed_path_length(authority + authority_length);
    parts->base = malloc(scheme + authority_length + path_length + 1);
    parts->host = malloc(authority_length + 1);
    if (parts->base == NULL || parts->host == NULL) {
        free_parts(parts);
        return UPSTREAM_OUT_OF_MEMORY;
    }
    memcpy(parts->base, upstream, scheme + authority_length);
    lower_case(parts->base, scheme + authority_length);
    memcpy(parts->base + scheme + authority_length, authority + authority_length, path_length);
    parts->base[scheme + authority_length + path_length] = '\0';
    memcpy(parts->host, parts->base + scheme, authority_length);
    parts->host[authority_length] = '\0';
    return UPSTREAM_PARSED;
}

static char *join_target(const char *base, const char *url) {
    size_t base_length = strlen(base);
    char *target = malloc(base_length + strlen(url) + 1);
    if (target != NULL) {
        memcpy(target, base, base_length);
        strcpy(target + base_length, url);
    }
    return target;
}

static cJSON *pass_through_answer(const upstream_parts *parts, const cJSON *url, const cJSON *headers,
                                  const cJSON *placeholder, double body_length) {
    cJSON *result = cJSON_CreateObject();
    char *target = join_target(parts->base, url->valuestring);
    cJSON *forwarded = forwarded_headers(headers, placeholder->valuestring, parts->host, body_length);
    if (result == NULL || target == NULL || forwarded == NULL || cJSON_AddStringToObject(result, "target", target) == NULL ||
        !cJSON_AddItemToObject(result, "headers", forwarded)) {
        cJSON_Delete(forwarded);
        cJSON_Delete(result);
        result = NULL;
    }
    free(target);
    return result;
}

typedef struct pass_through_arguments {
    const cJSON *upstream;
    const cJSON *url;
    const cJSON *headers;
    const cJSON *body_length;
    const cJSON *placeholder;
} pass_through_arguments;

static int read_pass_through_arguments(const spisor_call *call, pass_through_arguments *read, uint8_t **out,
                                       size_t *out_len, int32_t *status) {
    return service_require(call, "upstream", service_is_string, "a string", &read->upstream, out, out_len, status) &&
           service_require(call, "url", service_is_string, "a string", &read->url, out, out_len, status) &&
           service_require(call, "headers", service_is_object, "an object", &read->headers, out, out_len, status) &&
           service_require(call, "bodyLength", service_is_number, "a number", &read->body_length, out, out_len, status) &&
           service_require(call, "placeholderAuthorization", service_is_string, "a string", &read->placeholder, out,
                           out_len, status);
}

static int32_t answer_pass_through(const pass_through_arguments *arguments, uint8_t **out, size_t *out_len) {
    upstream_parts parts;
    upstream_parse parsed = split_upstream(arguments->upstream->valuestring, &parts);
    if (parsed == UPSTREAM_OUT_OF_MEMORY) {
        return service_out_of_memory(out, out_len);
    }
    if (parsed == UPSTREAM_REFUSED) {
        char cause[512];
        snprintf(cause, sizeof(cause), "the upstream %.400s is not an absolute http or https URL",
                 arguments->upstream->valuestring);
        return service_refuse(cause, REFUSAL_FIX, out, out_len);
    }
    int32_t status = service_answer(pass_through_answer(&parts, arguments->url, arguments->headers, arguments->placeholder,
                                                        arguments->body_length->valuedouble),
                                    out, out_len);
    free_parts(&parts);
    return status;
}

SERVICE_EXPORT int32_t forebay_library_routing_pass_through(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len) {
    spisor_call call;
    int32_t status = service_open(in, in_len, &call, out, out_len);
    if (status != SPISOR_STATUS_SUCCESS) {
        return status;
    }
    pass_through_arguments arguments;
    if (read_pass_through_arguments(&call, &arguments, out, out_len, &status)) {
        status = answer_pass_through(&arguments, out, out_len);
    }
    spisor_discard_call(&call);
    return status;
}

SERVICE_EXPORT int32_t forebay_library_routing_reply_headers(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len) {
    spisor_call call;
    int32_t status = service_open(in, in_len, &call, out, out_len);
    if (status != SPISOR_STATUS_SUCCESS) {
        return status;
    }
    const cJSON *headers = NULL;
    const cJSON *rewritten = NULL;
    if (service_require(&call, "headers", service_is_object, "an object", &headers, out, out_len, &status) &&
        service_require(&call, "rewritten", service_is_boolean, "a boolean", &rewritten, out, out_len, &status)) {
        status = service_answer(service_wrap("headers", routing_end_to_end_headers(headers, cJSON_IsTrue(rewritten))), out,
                                out_len);
    }
    spisor_discard_call(&call);
    return status;
}
