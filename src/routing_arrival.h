#ifndef ROUTING_ARRIVAL_H
#define ROUTING_ARRIVAL_H

#include "routing_support.h"

/**
 * What a proxy does with a request on arrival: refuse it unless it was addressed to the loopback
 * port by a name that is not a browser origin, answer the health probe, else let it route.
 */
SERVICE_EXPORT int32_t forebay_library_routing_arrival(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);

/** The port a proxy listens on, or the refusal naming why it cannot: a setting that is no port, or a listen failure. */
SERVICE_EXPORT int32_t forebay_library_routing_port(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);

#endif
