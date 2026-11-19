#ifndef ROUTING_ARRIVAL_H
#define ROUTING_ARRIVAL_H

#include "forebay/routing.h"
#include "routing_support.h"

/**
 * What a proxy does with a request on arrival: refuse it unless it was addressed to the loopback
 * port by a name that is not a browser origin, answer the health probe, else let it route.
 */
spisor_status routing_decide_arrival(const forebay_arrival_argument *argument, forebay_arrival_answer *answer);

/** The port a proxy listens on, or the refusal naming why it cannot: a setting that is no port, or a listen failure. */
int32_t routing_port_call(const cJSON *argument, service_reply *reply);

#endif
