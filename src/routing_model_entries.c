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

static int is_entered(const forebay_catalog_model *entries, size_t count, const char *id) {
    for (size_t index = 0; index < count; index++) {
        if (strcmp(entries[index].id, id) == 0) {
            return 1;
        }
    }
    return 0;
}

static int fill_entry(void **storage, forebay_catalog_model *entry, const forebay_catalog_model *model,
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

int routing_fill_model_entries(void **storage, const forebay_routing_profile *profile,
                               const forebay_catalog_provider *catalog, size_t catalog_count,
                               const forebay_catalog_model **entries, size_t *entry_count) {
    size_t total = catalog_model_total(catalog, catalog_count);
    forebay_catalog_model *filled = forebay_storage_array(storage, total, sizeof *filled);
    size_t count = 0;
    if (total > 0 && filled == NULL) {
        return 0;
    }
    for (size_t provider = 0; provider < catalog_count; provider++) {
        const forebay_catalog_provider *offered = &catalog[provider];
        for (size_t index = 0; index < offered->model_count; index++) {
            const forebay_catalog_model *model = &offered->models[index];
            if (is_entered(filled, count, model->id)) {
                continue;
            }
            if (!fill_entry(storage, &filled[count], model, profile)) {
                return 0;
            }
            count++;
        }
    }
    *entries = filled;
    *entry_count = count;
    return 1;
}

spisor_status routing_model_entries(void *context, const forebay_model_entries_argument *argument,
                                    forebay_model_entries_answer *answer, spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, MODEL_ENTRIES_SHAPE) ||
        !SPISOR_IS_EMPTY_COUNTED_ANSWER(answer, MODEL_ENTRIES_ANSWER_SHAPE, answer->entry_count)) {
        return spisor_refuse_shape(refusal, "routing's model_entries");
    }
    if (!routing_is_profile(argument->profile, 0)) {
        return forebay_refuse_member(refusal, "the profile", "complete, with every text set");
    }
    if (!routing_is_catalog(argument->catalog, argument->catalog_count)) {
        return forebay_refuse_member(refusal, "the catalog", "complete, with every text set");
    }
    if (!routing_fill_model_entries(&answer->storage, argument->profile, argument->catalog, argument->catalog_count,
                                    &answer->entries, &answer->entry_count)) {
        forebay_storage_release(answer->storage);
        SPISOR_CLEAR_ANSWER(answer, MODEL_ENTRIES_ANSWER_SHAPE);
        return SPISOR_OUT_OF_MEMORY;
    }
    return SPISOR_OK;
}

void routing_release_model_entries_answer(void *context, forebay_model_entries_answer *answer) {
    (void)context;
    if (answer != NULL && spisor_has_shape(answer->size, MODEL_ENTRIES_ANSWER_SHAPE)) {
        forebay_storage_release(answer->storage);
        SPISOR_CLEAR_ANSWER(answer, MODEL_ENTRIES_ANSWER_SHAPE);
    }
}
