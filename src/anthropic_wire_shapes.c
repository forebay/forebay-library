#include <stdio.h>
#include <string.h>

#include "anthropic_operations.h"
#include "definition_support.h"
#include "spisor/allocation.h"
#include "spisor/definition.h"

#define ERROR_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_error_argument, message)
#define LISTING_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_model_listing_argument, model_count)
#define LOOKUP_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_model_lookup_argument, id)
#define LOOKUP_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_model_lookup_answer, storage)
#define MEMBER_NAME_LIMIT 96

/** What a listing and a lookup read alike: the entries routing decided. */
typedef struct listing_view {
    const forebay_catalog_model *models;
    size_t model_count;
} listing_view;

/** The view of a listing or a lookup argument, which name these members alike. */
#define LISTING_VIEW_OF(argument) ((listing_view){(argument)->models, (argument)->model_count})

static const char LISTED_CREATED_AT[] = "2025-01-01T00:00:00Z";

cJSON *anthropic_error_object(anthropic_writer *writer, const char *error_type, const char *message) {
    cJSON *wire = anthropic_new_object(writer);
    cJSON *error = anthropic_new_object(writer);
    anthropic_set_text(writer, wire, "type", "error");
    anthropic_set_text(writer, error, "type", error_type);
    anthropic_set_text(writer, error, "message", message);
    anthropic_set(writer, wire, "error", error);
    return wire;
}

/** The wire's `error.type` of a reply, NULL for a kind reply.h does not name. */
static const char *error_type_of(uint32_t reply_kind) {
    switch (reply_kind) {
    case FOREBAY_REPLY_PROXY_ERROR:
        return "loader_proxy_error";
    case FOREBAY_REPLY_API_ERROR:
    case FOREBAY_REPLY_PROVIDER_FAILURE:
        return "api_error";
    case FOREBAY_REPLY_RATE_LIMITED:
        return "rate_limit_error";
    case FOREBAY_REPLY_INVALID_REQUEST:
        return "invalid_request_error";
    default:
        return NULL;
    }
}

spisor_status anthropic_error_envelope(void *context, const forebay_anthropic_error_argument *argument,
                                       forebay_anthropic_body_answer *answer, spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, ERROR_SHAPE) ||
        !SPISOR_IS_EMPTY_COUNTED_ANSWER(answer, ANTHROPIC_BODY_ANSWER_SHAPE, answer->length)) {
        return spisor_refuse_shape(refusal, "anthropic's error_envelope");
    }
    const char *error_type = error_type_of(argument->reply_kind);
    if (error_type == NULL) {
        return forebay_refuse_member(refusal, "reply_kind", "a forebay_reply_kind");
    }
    if (argument->message == NULL) {
        return forebay_refuse_member(refusal, "message", "a text");
    }
    anthropic_writer writer = {SPISOR_OK, NULL};
    cJSON *wire = anthropic_error_object(&writer, error_type, argument->message);
    return anthropic_finish_body(&writer, wire, answer, refusal);
}

/** The refusal of an entry's member named by its position; the longest name, a 20-digit index, fits the buffer. */
static spisor_status refuse_entry_member(spisor_refusal *refusal, size_t index, const char *member) {
    char name[MEMBER_NAME_LIMIT];
    snprintf(name, sizeof name, "models[%zu].%s", index, member);
    return forebay_refuse_member(refusal, name, "a text");
}

static spisor_status check_listing(const listing_view *view, spisor_refusal *refusal) {
    if (view->models == NULL && view->model_count > 0) {
        return forebay_refuse_member(refusal, "models", "a pointer to model_count entries");
    }
    for (size_t index = 0; index < view->model_count; index++) {
        if (view->models[index].id == NULL) {
            return refuse_entry_member(refusal, index, "id");
        }
        if (view->models[index].name == NULL) {
            return refuse_entry_member(refusal, index, "name");
        }
    }
    return SPISOR_OK;
}

static cJSON *listed_model_object(anthropic_writer *writer, const forebay_catalog_model *model) {
    cJSON *object = anthropic_new_object(writer);
    anthropic_set_text(writer, object, "type", "model");
    anthropic_set_text(writer, object, "id", model->id);
    anthropic_set_text(writer, object, "display_name", model->name);
    anthropic_set_text(writer, object, "created_at", LISTED_CREATED_AT);
    if (model->has_context) {
        anthropic_set_count(writer, object, "max_input_tokens", model->context);
    }
    if (model->has_output) {
        anthropic_set_count(writer, object, "max_tokens", model->output);
    }
    return object;
}

static cJSON *listed_models(anthropic_writer *writer, const listing_view *view) {
    cJSON *listed = anthropic_new_array(writer);
    for (size_t index = 0; index < view->model_count; index++) {
        anthropic_append(writer, listed, listed_model_object(writer, &view->models[index]));
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
        !SPISOR_IS_EMPTY_COUNTED_ANSWER(answer, ANTHROPIC_BODY_ANSWER_SHAPE, answer->length)) {
        return spisor_refuse_shape(refusal, "anthropic's model_listing");
    }
    listing_view view = LISTING_VIEW_OF(argument);
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
    return anthropic_finish_body(&writer, wire, answer, refusal);
}

/** The first entry `id` names, NULL for none. */
static const forebay_catalog_model *entry_named(const listing_view *view, const char *id) {
    for (size_t index = 0; index < view->model_count; index++) {
        if (strcmp(view->models[index].id, id) == 0) {
            return &view->models[index];
        }
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
    cJSON *wire = anthropic_error_object(writer, "not_found_error", message);
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
        !SPISOR_IS_EMPTY_COUNTED_ANSWER(answer, LOOKUP_ANSWER_SHAPE, answer->length)) {
        return spisor_refuse_shape(refusal, "anthropic's model_lookup");
    }
    listing_view view = LISTING_VIEW_OF(argument);
    spisor_status checked = check_lookup(argument, &view, refusal);
    if (checked != SPISOR_OK) {
        return checked;
    }
    anthropic_writer writer = {SPISOR_OK, NULL};
    const forebay_catalog_model *entry = entry_named(&view, argument->id);
    cJSON *wire = entry != NULL ? listed_model_object(&writer, entry) : not_found_object(&writer, argument->id);
    forebay_anthropic_body_answer body = SPISOR_INIT(forebay_anthropic_body_answer);
    spisor_status status = anthropic_finish_body(&writer, wire, &body, refusal);
    if (status == SPISOR_OK) {
        answer->bytes = body.bytes;
        answer->length = body.length;
        answer->storage = body.storage;
        answer->found = entry != NULL ? 1 : 0;
    }
    return status;
}

ANTHROPIC_RELEASE_ANSWER(anthropic_release_model_lookup, forebay_anthropic_model_lookup_answer, LOOKUP_ANSWER_SHAPE)
