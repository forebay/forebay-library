#ifndef ROUTING_LIMITS_H
#define ROUTING_LIMITS_H

#include "routing_support.h"

/**
 * The bounds every host applies to what a client sends its proxy, so a local process cannot grow a
 * host's memory without limit.
 */
SERVICE_EXPORT int32_t routing_limits(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);

#endif
