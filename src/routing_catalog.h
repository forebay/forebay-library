#ifndef ROUTING_CATALOG_H
#define ROUTING_CATALOG_H

#include "forebay/routing.h"

spisor_status routing_catalog_refresh(void *context, const forebay_catalog_argument *argument,
                                      forebay_catalog_answer *answer, spisor_refusal *refusal);
void routing_release_catalog_answer(void *context, forebay_catalog_answer *answer);

#endif
