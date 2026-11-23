#ifndef ROUTING_CATALOG_H
#define ROUTING_CATALOG_H

#include "forebay/routing.h"

spisor_status routing_catalog_open(void *context, const forebay_catalog_open_argument *argument,
                                   forebay_catalog_open_answer *answer, spisor_refusal *refusal);
spisor_status routing_catalog_event(void *context, const forebay_catalog_event_argument *argument,
                                    forebay_catalog_event_answer *answer, spisor_refusal *refusal);
spisor_status routing_catalog_read(void *context, const forebay_catalog_read_argument *argument,
                                   forebay_catalog_read_answer *answer, spisor_refusal *refusal);
void routing_release_catalog_open_answer(void *context, forebay_catalog_open_answer *answer);
void routing_release_catalog_event_answer(void *context, forebay_catalog_event_answer *answer);
void routing_release_catalog_read_answer(void *context, forebay_catalog_read_answer *answer);
void routing_release_catalog(void *context, forebay_catalog *catalog);

#endif
