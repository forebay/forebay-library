#ifndef ROUTING_FAILURE_H
#define ROUTING_FAILURE_H

#include "routing_support.h"

/**
 * The reply, and the log lines, for a failure a proxy meets outside any chain entry: an upstream it
 * could not reach while passing a request through, or a request that failed before it was answered.
 */
spisor_status routing_failure_call(const cJSON *argument, service_reply *reply);

#endif
