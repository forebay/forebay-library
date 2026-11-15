#ifndef ROUTING_STEP_H
#define ROUTING_STEP_H

#include "routing_support.h"

/** The step of a call already parsed, for the definition and the JSON export alike. */
int32_t routing_step_call(const spisor_call *call, uint8_t **out, size_t *out_len);

SERVICE_EXPORT int32_t forebay_library_routing_step(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);

#endif
