#ifndef ROUTING_RESOLVE_H
#define ROUTING_RESOLVE_H

#include "routing_support.h"

SERVICE_EXPORT int32_t routing_resolve(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);

#endif
