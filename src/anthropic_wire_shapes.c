#include <stdio.h>
#include <string.h>

#include "anthropic_operations.h"
#include "definition_support.h"
#include "spisor/definition.h"

#define ERROR_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_error_argument, message)
#define LISTING_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_model_listing_argument, provider_count)
#define MEMBER_NAME_LIMIT 96

static const char LISTED_CREATED_AT[] = "2025-01-01T00:00:00Z";

/** Fills the answer from a built wire object, which it deletes, or answers the writer's failure, which is memory. */
static spisor_status finish_shape(const anthropic_writer *writer, cJSON *wire, forebay_anthropic_body_answer *answer) {
    if (writer->status != SPISOR_OK) {
        cJSON_Delete(wire);
        return writer->status;
    }
    return anthropic_write_body(wire, answer);
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
    cJSON *wire = anthropic_new_object(&writer);
    cJSON *error = anthropic_new_object(&writer);
    anthropic_set_text(&writer, wire, "type", "error");
    anthropic_set_text(&writer, error, "type", argument->error_type);
    anthropic_set_text(&writer, error, "message", argument->message);
    anthropic_set(&writer, wire, "error", error);
    return finish_shape(&writer, wire, answer);
}

static spisor_status refuse_listed_member(spisor_refusal *refusal, size_t provider_index, const char *tail,
                                          const char *rule) {
    char member[MEMBER_NAME_LIMIT];
    snprintf(member, sizeof member, "providers[%zu]%s", provider_index, tail);
    return forebay_refuse_member(refusal, member, rule);
}

static spisor_status refuse_listed_model(spisor_refusal *refusal, size_t provider_index, size_t model_index) {
    char tail[MEMBER_NAME_LIMIT];
    snprintf(tail, sizeof tail, ".models[%zu].id", model_index);
    return refuse_listed_member(refusal, provider_index, tail, "a text");
}

static spisor_status check_listed_provider(const forebay_anthropic_listed_provider *provider, size_t provider_index,
                                           spisor_refusal *refusal) {
    if (provider->models == NULL && provider->model_count > 0) {
        return refuse_listed_member(refusal, provider_index, ".models", "a pointer to model_count models");
    }
    for (size_t index = 0; index < provider->model_count; index++) {
        if (provider->models[index].id == NULL) {
            return refuse_listed_model(refusal, provider_index, index);
        }
    }
    return SPISOR_OK;
}

static spisor_status check_listing(const forebay_anthropic_model_listing_argument *argument,
                                   spisor_refusal *refusal) {
    if (argument->providers == NULL && argument->provider_count > 0) {
        return forebay_refuse_member(refusal, "providers", "a pointer to provider_count providers");
    }
    for (size_t index = 0; index < argument->provider_count; index++) {
        spisor_status status = check_listed_provider(&argument->providers[index], index, refusal);
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
                                  const forebay_anthropic_model_listing_argument *argument) {
    cJSON *object = anthropic_new_object(writer);
    anthropic_set_text(writer, object, "type", "model");
    anthropic_set_text(writer, object, "id", model->id);
    anthropic_set_text(writer, object, "display_name", forebay_is_text(model->name) ? model->name : model->id);
    anthropic_set_text(writer, object, "created_at", LISTED_CREATED_AT);
    anthropic_set_count(writer, object, "max_input_tokens",
                        model->has_context == 1 ? model->context : argument->default_context);
    anthropic_set_count(writer, object, "max_tokens",
                        model->has_output == 1 ? model->output : argument->default_output);
    return object;
}

/** The models of every provider, each id once as the first provider offers it. */
static cJSON *listed_models(anthropic_writer *writer, const forebay_anthropic_model_listing_argument *argument) {
    cJSON *listed = anthropic_new_array(writer);
    for (size_t provider = 0; provider < argument->provider_count; provider++) {
        const forebay_anthropic_listed_provider *offered = &argument->providers[provider];
        for (size_t index = 0; index < offered->model_count; index++) {
            if (!is_listed(listed, offered->models[index].id)) {
                anthropic_append(writer, listed, listed_model_object(writer, &offered->models[index], argument));
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

spisor_status anthropic_model_listing(void *context, const forebay_anthropic_model_listing_argument *argument,
                                      forebay_anthropic_body_answer *answer, spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, LISTING_SHAPE) ||
        !SPISOR_IS_EMPTY_ANSWER(answer, ANTHROPIC_BODY_ANSWER_SHAPE)) {
        return spisor_refuse_shape(refusal, "anthropic's model_listing");
    }
    spisor_status checked = check_listing(argument, refusal);
    if (checked != SPISOR_OK) {
        return checked;
    }
    anthropic_writer writer = {SPISOR_OK, NULL};
    cJSON *wire = anthropic_new_object(&writer);
    anthropic_set(&writer, wire, "data", listed_models(&writer, argument));
    const cJSON *attached = cJSON_GetObjectItemCaseSensitive(wire, "data");
    anthropic_set_text_or_null(&writer, wire, "first_id", edge_id(attached, 0));
    anthropic_set_text_or_null(&writer, wire, "last_id", edge_id(attached, 1));
    anthropic_set_flag(&writer, wire, "has_more", 0);
    return finish_shape(&writer, wire, answer);
}
