#ifndef ROUTING_HEADERS_H
#define ROUTING_HEADERS_H

#include "routing_support.h"

SERVICE_EXPORT int32_t forebay_library_routing_pass_through(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);
SERVICE_EXPORT int32_t forebay_library_routing_reply_headers(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);

/**
 * The string headers fit to forward across a connection of their own: the hop-by-hop set, every
 * `proxy-` name and every name the `connection` header lists are dropped, names compared ignoring
 * case. A body the caller rewrote also drops `content-encoding` and `content-length`.
 * Answers a new object, or NULL when memory runs out.
 */
cJSON *routing_end_to_end_headers(const cJSON *headers, int rewritten);

#endif
