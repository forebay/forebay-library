#ifndef ROUTING_FAILURE_H
#define ROUTING_FAILURE_H

#include "routing_support.h"

/**
 * The reply, and the log lines, for a failure a proxy meets outside any chain entry: an upstream it
 * could not reach while passing a request through, or a request that failed before it was answered.
 */
SERVICE_EXPORT int32_t forebay_library_routing_failure(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);

#endif
