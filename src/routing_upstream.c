#include "routing_upstream.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCHEME_HTTP "http://"
#define SCHEME_HTTPS "https://"
#define UPSTREAM_FIX "give the profile an upstream such as https://api.example.com"

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

static char *copy_of(const char *text, size_t length) {
    char *copy = malloc(length + 1);
    if (copy != NULL) {
        memcpy(copy, text, length);
        copy[length] = '\0';
    }
    return copy;
}

void routing_free_upstream(routing_upstream *parts) {
    free(parts->origin);
    free(parts->base);
    free(parts->host);
    parts->origin = NULL;
    parts->base = NULL;
    parts->host = NULL;
}

typedef enum upstream_parse {
    UPSTREAM_PARSED,
    UPSTREAM_REFUSED,
    UPSTREAM_OUT_OF_MEMORY
} upstream_parse;

static upstream_parse split_upstream(const char *upstream, routing_upstream *parts) {
    parts->origin = NULL;
    parts->base = NULL;
    parts->host = NULL;
    size_t scheme = scheme_length(upstream);
    const char *authority = upstream + scheme;
    size_t authority_length = strcspn(authority, "/?#");
    if (scheme == 0 || authority_length == 0 || memchr(authority, '@', authority_length) != NULL) {
        return UPSTREAM_REFUSED;
    }
    size_t origin_length = scheme + authority_length;
    size_t path_length = trimmed_path_length(authority + authority_length);
    parts->base = malloc(origin_length + path_length + 1);
    if (parts->base == NULL) {
        return UPSTREAM_OUT_OF_MEMORY;
    }
    memcpy(parts->base, upstream, origin_length);
    lower_case(parts->base, origin_length);
    memcpy(parts->base + origin_length, authority + authority_length, path_length);
    parts->base[origin_length + path_length] = '\0';
    parts->origin = copy_of(parts->base, origin_length);
    parts->host = copy_of(parts->base + scheme, authority_length);
    if (parts->origin == NULL || parts->host == NULL) {
        routing_free_upstream(parts);
        return UPSTREAM_OUT_OF_MEMORY;
    }
    return UPSTREAM_PARSED;
}

static int32_t refuse_upstream(const char *upstream, uint8_t **out, size_t *out_len) {
    char cause[512];
    snprintf(cause, sizeof(cause), "the upstream %.400s is not an absolute http or https URL", upstream);
    return service_refuse(cause, UPSTREAM_FIX, out, out_len);
}

int routing_read_upstream(const char *upstream, routing_upstream *parts, uint8_t **out, size_t *out_len,
                          int32_t *status) {
    upstream_parse parsed = split_upstream(upstream, parts);
    if (parsed == UPSTREAM_OUT_OF_MEMORY) {
        *status = service_out_of_memory(out, out_len);
    } else if (parsed == UPSTREAM_REFUSED) {
        *status = refuse_upstream(upstream, out, out_len);
    }
    return parsed == UPSTREAM_PARSED;
}
