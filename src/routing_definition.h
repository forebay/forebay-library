#ifndef ROUTING_DEFINITION_H
#define ROUTING_DEFINITION_H

#include <stddef.h>

#include "cJSON.h"
#include "forebay/routing.h"
#include "service_support.h"

/** One of routing's decisions over the cJSON argument its definition built. */
typedef spisor_status (*routing_decision)(const cJSON *argument, service_reply *reply);

/** Fills an answer from a decision's result into the answer's own storage chain; 0 when memory runs out. */
typedef int (*routing_answer_filler)(const cJSON *result, void *answer);

/**
 * Runs `decision` over `argument`, which it deletes, and fills the answer whose first shape of `shape` bytes holds
 * its `storage` at `storage`. A NULL argument is memory that ran out while it was built.
 *
 * @return OK; REFUSED or MALFORMED with the refusal filled from the decision; OUT_OF_MEMORY with the answer empty.
 */
spisor_status routing_decide(routing_decision decision, cJSON *argument, routing_answer_filler fill, void *answer,
                             void **storage, size_t shape, spisor_refusal *refusal);

spisor_status routing_step(void *context, const forebay_step_argument *argument, forebay_step_answer *answer,
                           spisor_refusal *refusal);
void routing_release_step_answer(void *context, forebay_step_answer *answer);
void routing_release_walk(void *context, forebay_walk *walk);

#endif
