#include <stdio.h>
#include <string.h>

#include "anthropic_operations.h"
#include "definition_support.h"
#include "spisor/allocation.h"
#include "spisor/definition.h"

#define ERROR_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_error_argument, message)
#define LISTING_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_model_listing_argument, provider_count)
#define LOOKUP_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_model_lookup_argument, id)
#define LOOKUP_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_model_lookup_answer, storage)
#define MEMBER_NAME_LIMIT 96

/** What a listing and a lookup read alike: the providers and the defaults for a model that gives no limit. */
typedef struct listing_view {
    uint64_t default_context;
    uint64_t default_output;
    const forebay_anthropic_listed_provider *providers;
    size_t provider_count;
} listing_view;

static const char LISTED_CREATED_AT[] = "2025-01-01T00:00:00Z";

/** Fills the answer from a built wire object, which it deletes, or answers the writer's failure, which is memory. */
static spisor_status finish_shape(const anthropic_writer *writer, cJSON *wire, forebay_anthropic_body_answer *answer) {
    if (writer->status != SPISOR_OK) {
        cJSON_Delete(wire);
        return writer->status;
    }
    return anthropic_write_body(wire, answer);
}

/** The wire object of an Anthropic error, NULL once the writer has failed. */
static cJSON *error_object(anthropic_writer *writer, const char *error_type, const char *message) {
    cJSON *wire = anthropic_new_object(writer);
    cJSON *error = anthropic_new_object(writer);
    anthropic_set_text(writer, wire, "type", "error");
    anthropic_set_text(writer, error, "type", error_type);
    anthropic_set_text(writer, error, "message", message);
    anthropic_set(writer, wire, "error", error);
    return wire;
}

spisor_status anthropic_error_envelope(void *context, const forebay_anthropic_error_argument *argument,
                                       forebay_anthropic_body_answer *answer, spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, ERROR_SHAPE) ||
        !SPISOR_IS_EMPTY_ANSWER(answer, ANTHROPIC_BODY_ANSWER_SHAPE)) {
        return spisor_refuse_shape(refusal, "anthropic's error_envelope");
    }
    if (!forebay_is_text(argument->error_type)) {
        return forebay_refuse_member(refusal, "error_type", "an Anthropic error type, such as api_error");
    }
    if (argument->message == NULL) {
        return forebay_refuse_member(refusal, "message", "a text");
    }
    anthropic_writer writer = {SPISOR_OK, NULL};
    cJSON *wire = error_object(&writer, argument->error_type, argument->message);
    return finish_shape(&writer, wire, answer);
}

/** The refusal of a model id named by its position; the longest name, two 20-digit indexes, fits the buffer. */
static spisor_status refuse_model_id(spisor_refusal *refusal, size_t provider_index, size_t model_index) {
    char member[MEMBER_NAME_LIMIT];
    snprintf(member, sizeof member, "providers[%zu].models[%zu].id", provider_index, model_index);
    return forebay_refuse_member(refusal, member, "a text");
}

static spisor_status refuse_provider_models(spisor_refusal *refusal, size_t provider_index) {
    char member[MEMBER_NAME_LIMIT];
    snprintf(member, sizeof member, "providers[%zu].models", provider_index);
    return forebay_refuse_member(refusal, member, "a pointer to model_count models");
}

static spisor_status check_listed_provider(const forebay_anthropic_listed_provider *provider, size_t provider_index,
                                           spisor_refusal *refusal) {
    if (provider->models == NULL && provider->model_count > 0) {
        return refuse_provider_models(refusal, provider_index);
    }
    for (size_t index = 0; index < provider->model_count; index++) {
        if (provider->models[index].id == NULL) {
            return refuse_model_id(refusal, provider_index, index);
        }
    }
    return SPISOR_OK;
}

static spisor_status check_listing(const listing_view *view,
                                   spisor_refusal *refusal) {
    if (view->providers == NULL && view->provider_count > 0) {
        return forebay_refuse_member(refusal, "providers", "a pointer to provider_count providers");
    }
    for (size_t index = 0; index < view->provider_count; index++) {
        spisor_status status = check_listed_provider(&view->providers[index], index, refusal);
        if (status != SPISOR_OK) {
            return status;
        }
    }
    return SPISOR_OK;
}

static int is_listed(const cJSON *listed, const char *id) {
    const cJSON *model = NULL;
    cJSON_ArrayForEach(model, listed) {
        if (strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(model, "id")), id) == 0) {
            return 1;
        }
    }
    return 0;
}

static cJSON *listed_model_object(anthropic_writer *writer, const forebay_anthropic_listed_model *model,
                                  const listing_view *view) {
    cJSON *object = anthropic_new_object(writer);
    anthropic_set_text(writer, object, "type", "model");
    anthropic_set_text(writer, object, "id", model->id);
    anthropic_set_text(writer, object, "display_name", forebay_is_text(model->name) ? model->name : model->id);
    anthropic_set_text(writer, object, "created_at", LISTED_CREATED_AT);
    anthropic_set_count(writer, object, "max_input_tokens",
                        model->has_context == 1 ? model->context : view->default_context);
    anthropic_set_count(writer, object, "max_tokens",
                        model->has_output == 1 ? model->output : view->default_output);
    return object;
}

/** The models of every provider, each id once as the first provider offers it. */
static cJSON *listed_models(anthropic_writer *writer, const listing_view *view) {
    cJSON *listed = anthropic_new_array(writer);
    for (size_t provider = 0; provider < view->provider_count; provider++) {
        const forebay_anthropic_listed_provider *offered = &view->providers[provider];
        for (size_t index = 0; index < offered->model_count; index++) {
            if (!is_listed(listed, offered->models[index].id)) {
                anthropic_append(writer, listed, listed_model_object(writer, &offered->models[index], view));
            }
        }
    }
    return listed;
}

/** The id of the first or last of the listed models, NULL for none. */
static const char *edge_id(const cJSON *listed, int last) {
    int index = last ? cJSON_GetArraySize(listed) - 1 : 0;
    return cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(listed, index), "id"));
}

static listing_view view_of_listing(const forebay_anthropic_model_listing_argument *argument) {
    return (listing_view){argument->default_context, argument->default_output, argument->providers,
                          argument->provider_count};
}

spisor_status anthropic_model_listing(void *context, const forebay_anthropic_model_listing_argument *argument,
                                      forebay_anthropic_body_answer *answer, spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, LISTING_SHAPE) ||
        !SPISOR_IS_EMPTY_ANSWER(answer, ANTHROPIC_BODY_ANSWER_SHAPE)) {
        return spisor_refuse_shape(refusal, "anthropic's model_listing");
    }
    listing_view view = view_of_listing(argument);
    spisor_status checked = check_listing(&view, refusal);
    if (checked != SPISOR_OK) {
        return checked;
    }
    anthropic_writer writer = {SPISOR_OK, NULL};
    cJSON *wire = anthropic_new_object(&writer);
    anthropic_set(&writer, wire, "data", listed_models(&writer, &view));
    const cJSON *attached = cJSON_GetObjectItemCaseSensitive(wire, "data");
    anthropic_set_text_or_null(&writer, wire, "first_id", edge_id(attached, 0));
    anthropic_set_text_or_null(&writer, wire, "last_id", edge_id(attached, 1));
    anthropic_set_flag(&writer, wire, "has_more", 0);
    return finish_shape(&writer, wire, answer);
}

/** The listed model `id` names, detached from `listed` and so the caller's to delete, NULL for none. */
static cJSON *detach_listed(cJSON *listed, const char *id) {
    int index = 0;
    const cJSON *model = NULL;
    cJSON_ArrayForEach(model, listed) {
        if (strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(model, "id")), id) == 0) {
            return cJSON_DetachItemFromArray(listed, index);
        }
        index++;
    }
    return NULL;
}

/** The wire object of the not_found_error for `id`, NULL once the writer has failed. */
static cJSON *not_found_object(anthropic_writer *writer, const char *id) {
    static const char prefix[] = "model not found: ";
    size_t prefix_length = sizeof prefix - 1;
    size_t id_length = strlen(id);
    char *message = spisor_memory_allocate(prefix_length + id_length + 1);
    if (message == NULL) {
        writer->status = SPISOR_OUT_OF_MEMORY;
        return NULL;
    }
    memcpy(message, prefix, prefix_length);
    memcpy(message + prefix_length, id, id_length + 1);
    cJSON *wire = error_object(writer, "not_found_error", message);
    spisor_memory_release(message);
    return wire;
}

static spisor_status check_lookup(const forebay_anthropic_model_lookup_argument *argument, const listing_view *view,
                                  spisor_refusal *refusal) {
    spisor_status checked = check_listing(view, refusal);
    if (checked == SPISOR_OK && argument->id == NULL) {
        checked = forebay_refuse_member(refusal, "id", "a text");
    }
    return checked;
}

spisor_status anthropic_model_lookup(void *context, const forebay_anthropic_model_lookup_argument *argument,
                                     forebay_anthropic_model_lookup_answer *answer, spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, LOOKUP_SHAPE) ||
        !SPISOR_IS_EMPTY_ANSWER(answer, LOOKUP_ANSWER_SHAPE)) {
        return spisor_refuse_shape(refusal, "anthropic's model_lookup");
    }
    listing_view view = {argument->default_context, argument->default_output, argument->providers,
                         argument->provider_count};
    spisor_status checked = check_lookup(argument, &view, refusal);
    if (checked != SPISOR_OK) {
        return checked;
    }
    anthropic_writer writer = {SPISOR_OK, NULL};
    cJSON *listed = listed_models(&writer, &view);
    cJSON *wire = writer.status == SPISOR_OK ? detach_listed(listed, argument->id) : NULL;
    uint32_t found = wire != NULL ? 1 : 0;
    if (writer.status == SPISOR_OK && !found) {
        wire = not_found_object(&writer, argument->id);
    }
    cJSON_Delete(listed);
    forebay_anthropic_body_answer body = SPISOR_INIT(forebay_anthropic_body_answer);
    spisor_status status = finish_shape(&writer, wire, &body);
    if (status == SPISOR_OK) {
        answer->bytes = body.bytes;
        answer->length = body.length;
        answer->storage = body.storage;
        answer->found = found;
    }
    return status;
}

ANTHROPIC_RELEASE_ANSWER(anthropic_release_model_lookup, forebay_anthropic_model_lookup_answer, LOOKUP_ANSWER_SHAPE)
