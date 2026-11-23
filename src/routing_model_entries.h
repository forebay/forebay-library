#ifndef ROUTING_MODEL_ENTRIES_H
#define ROUTING_MODEL_ENTRIES_H

#include "forebay/routing.h"

spisor_status routing_model_entries(void *context, const forebay_model_entries_argument *argument,
                                    forebay_model_entries_answer *answer, spisor_refusal *refusal);
void routing_release_model_entries_answer(void *context, forebay_model_entries_answer *answer);

/** The entries `model_entries` lists over the catalog, owned by `*storage`; 0 when memory runs out. */
int routing_fill_model_entries(void **storage, const forebay_routing_profile *profile,
                               const forebay_catalog_provider *catalog, size_t catalog_count,
                               const forebay_catalog_model **entries, size_t *entry_count);

#endif
