#ifndef ROUTING_OUTCOME_H
#define ROUTING_OUTCOME_H

#include "routing_support.h"

SERVICE_EXPORT int32_t routing_outcome(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);

#endif
