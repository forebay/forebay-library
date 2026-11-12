#ifndef ROUTING_OUTCOME_H
#define ROUTING_OUTCOME_H

#include "routing_support.h"

/**
 * {rateLimited, resetAt?} for an argument whose status, headers and now are already read as a number, an
 * object and a number; NULL when memory runs out.
 */
cJSON *routing_outcome_of(const cJSON *argument);

SERVICE_EXPORT int32_t forebay_library_routing_outcome(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);

#endif
