#ifndef ROUTING_ENVIRONMENT_H
#define ROUTING_ENVIRONMENT_H

#include "routing_support.h"

SERVICE_EXPORT int32_t routing_environment(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);

#endif
