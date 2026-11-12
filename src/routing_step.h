#ifndef ROUTING_STEP_H
#define ROUTING_STEP_H

#include "routing_support.h"

SERVICE_EXPORT int32_t forebay_library_routing_step(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);

#endif
