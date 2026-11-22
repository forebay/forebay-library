#include "routing_model_entries.h"

#include <string.h>

#include "answer_storage.h"
#include "definition_support.h"
#include "routing_arguments.h"
#include "spisor/definition.h"

#define MODEL_ENTRIES_SHAPE SPISOR_SHAPE_THROUGH(forebay_model_entries_argument, catalog_count)
#define MODEL_ENTRIES_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_model_entries_answer, storage)
#define PROFILE_DEFAULTS_SHAPE SPISOR_SHAPE_THROUGH(forebay_routing_profile, default_output)

/** A limit as an entry holds it: the model's own, else the profile's default when it has one. */
typedef struct entry_limit {
    uint32_t present;
    uint64_t value;
} entry_limit;

static entry_limit limit_of(uint32_t has_own, uint64_t own, int has_default, uint64_t fallback) {
    entry_limit limit = {0, 0};
    if (has_own) {
        limit.present = 1;
        limit.value = own;
    } else if (has_default) {
        limit.present = 1;
        limit.value = fallback;
    }
    return limit;
}

static size_t catalog_model_total(const forebay_catalog_provider *catalog, size_t count) {
    size_t total = 0;
    for (size_t index = 0; index < count; index++) {
        total += catalog[index].model_count;
    }
    return total;
}

static int is_entered(const forebay_model_entry *entries, size_t count, const char *id) {
    for (size_t index = 0; index < count; index++) {
        if (strcmp(entries[index].id, id) == 0) {
            return 1;
        }
    }
    return 0;
}

static int fill_entry(void **storage, forebay_model_entry *entry, const forebay_catalog_model *model,
                      const forebay_routing_profile *profile) {
    int has_defaults = spisor_has_shape(profile->size, PROFILE_DEFAULTS_SHAPE);
    entry_limit context = limit_of(model->has_context, model->context, has_defaults,
                                   has_defaults ? profile->default_context : 0);
    entry_limit output = limit_of(model->has_output, model->output, has_defaults,
                                  has_defaults ? profile->default_output : 0);
    entry->has_context = context.present;
    entry->context = context.value;
    entry->has_output = output.present;
    entry->output = output.value;
    return forebay_storage_text(storage, model->id, &entry->id) &&
           forebay_storage_text(storage, forebay_is_text(model->name) ? model->name : model->id, &entry->name);
}

/** Fills the entries into the answer's storage; 0, with the storage released, when memory runs out. */
static int fill_entries(const forebay_model_entries_argument *argument, forebay_model_entries_answer *answer) {
    size_t total = catalog_model_total(argument->catalog, argument->catalog_count);
    forebay_model_entry *entries = forebay_storage_array(&answer->storage, total, sizeof *entries);
    size_t count = 0;
    if (total > 0 && entries == NULL) {
        return 0;
    }
    for (size_t provider = 0; provider < argument->catalog_count; provider++) {
        const forebay_catalog_provider *offered = &argument->catalog[provider];
        for (size_t index = 0; index < offered->model_count; index++) {
            const forebay_catalog_model *model = &offered->models[index];
            if (is_entered(entries, count, model->id)) {
                continue;
            }
            if (!fill_entry(&answer->storage, &entries[count], model, argument->profile)) {
                forebay_storage_release(answer->storage);
                answer->storage = NULL;
                return 0;
            }
            count++;
        }
    }
    answer->entries = entries;
    answer->entry_count = count;
    return 1;
}

spisor_status routing_model_entries(void *context, const forebay_model_entries_argument *argument,
                                    forebay_model_entries_answer *answer, spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, MODEL_ENTRIES_SHAPE) ||
        !SPISOR_IS_EMPTY_COUNTED_ANSWER(answer, MODEL_ENTRIES_ANSWER_SHAPE, answer->entry_count)) {
        return spisor_refuse_shape(refusal, "routing's model_entries");
    }
    if (!routing_is_profile(argument->profile, 0) || !routing_is_catalog(argument->catalog, argument->catalog_count)) {
        return forebay_refuse_member(refusal, "the profile or the catalog", "complete, with every text set");
    }
    return fill_entries(argument, answer) ? SPISOR_OK : SPISOR_OUT_OF_MEMORY;
}

void routing_release_model_entries_answer(void *context, forebay_model_entries_answer *answer) {
    (void)context;
    if (answer != NULL && spisor_has_shape(answer->size, MODEL_ENTRIES_ANSWER_SHAPE)) {
        forebay_storage_release(answer->storage);
        SPISOR_CLEAR_ANSWER(answer, MODEL_ENTRIES_ANSWER_SHAPE);
    }
}
