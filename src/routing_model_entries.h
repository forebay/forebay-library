#ifndef ROUTING_MODEL_ENTRIES_H
#define ROUTING_MODEL_ENTRIES_H

#include "forebay/routing.h"

spisor_status routing_model_entries(void *context, const forebay_model_entries_argument *argument,
                                    forebay_model_entries_answer *answer, spisor_refusal *refusal);
void routing_release_model_entries_answer(void *context, forebay_model_entries_answer *answer);

#endif
