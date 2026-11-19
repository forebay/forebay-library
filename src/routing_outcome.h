#ifndef ROUTING_OUTCOME_H
#define ROUTING_OUTCOME_H

#include "routing_support.h"

/**
 * {rateLimited, resetAt?} for an argument whose status, headers and now are already read as a number, an
 * object and a number; NULL when memory runs out.
 */
cJSON *routing_outcome_of(const cJSON *argument);

spisor_status routing_outcome_call(const cJSON *argument, service_reply *reply);

#endif
