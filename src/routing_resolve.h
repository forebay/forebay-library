#ifndef ROUTING_RESOLVE_H
#define ROUTING_RESOLVE_H

#include "routing_support.h"

/** The profile members routing_resolution reads, for routing_read_profile. */
#define ROUTING_RESOLUTION_NEEDS (NEEDS_NATIVE_PATTERN | NEEDS_UNSERVED_PATTERN)

/**
 * {chain, notices} for the requested model, or NULL with refusal filled when a profile pattern is refused,
 * or with it clear when memory runs out.
 */
cJSON *routing_resolution(const routing_profile *profile, const cJSON *map, const cJSON *models, const char *requested,
                          routing_refusal *refusal);

SERVICE_EXPORT int32_t forebay_library_routing_resolve(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);

#endif
