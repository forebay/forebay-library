#ifndef ANTHROPIC_ROUTE_H
#define ANTHROPIC_ROUTE_H

#include "forebay/anthropic.h"

spisor_status anthropic_route_request(void *context, const forebay_anthropic_route_argument *argument,
                                      forebay_anthropic_route_answer *answer, spisor_refusal *refusal);
void anthropic_release_route(void *context, forebay_anthropic_route_answer *answer);

#endif
