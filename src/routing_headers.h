#ifndef ROUTING_HEADERS_H
#define ROUTING_HEADERS_H

#include "routing_support.h"

int32_t routing_pass_through_call(const cJSON *argument, service_reply *reply);
int32_t routing_reply_headers_call(const cJSON *argument, service_reply *reply);

/**
 * The string headers fit to forward across a connection of their own: the hop-by-hop set, every
 * `proxy-` name and every name the `connection` header lists are dropped, names compared ignoring
 * case. A body the caller rewrote also drops `content-encoding` and `content-length`.
 * Answers a new object, or NULL when memory runs out.
 */
cJSON *routing_end_to_end_headers(const cJSON *headers, int rewritten);

#endif
