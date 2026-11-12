#ifndef ROUTING_UPSTREAM_H
#define ROUTING_UPSTREAM_H

#include "routing_support.h"

/**
 * The parts of an absolute http(s) upstream URL a proxy forwards to, each a new string: its origin
 * (scheme, host and port), the base a request path is appended to (the origin and the path without
 * its trailing slashes) and the Host header value, with the scheme and host lower-cased.
 */
typedef struct routing_upstream {
    char *origin;
    char *base;
    char *host;
} routing_upstream;

/**
 * Parses upstream into parts; a relative URL, another scheme or one carrying userinfo is refused.
 * On a problem answers 0 with the refusal or out of memory already answered into *status.
 */
int routing_read_upstream(const char *upstream, routing_upstream *parts, uint8_t **out, size_t *out_len,
                          int32_t *status);

void routing_free_upstream(routing_upstream *parts);

#endif
