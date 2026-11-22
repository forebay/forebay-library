#include "anthropic_route.h"

#include <string.h>

#include "anthropic_header_names.h"
#include "anthropic_operations.h"
#include "definition_support.h"
#include "spisor/definition.h"

#define ARGUMENT_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_route_argument, path)
#define ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_route_answer, storage)

#define MESSAGES_PATH "/v1/messages"
#define MODELS_PATH "/v1/models"

static int is_path(const char *path, size_t length, const char *wanted) {
    return length == strlen(wanted) && strncmp(path, wanted, length) == 0;
}

static int has_prefix(const char *path, size_t length, const char *prefix) {
    size_t prefix_length = strlen(prefix);
    return length >= prefix_length && strncmp(path, prefix, prefix_length) == 0;
}

static uint32_t kind_of(const char *method, const char *path, size_t length) {
    int is_get = anthropic_names_match(method, "GET");
    if (anthropic_names_match(method, "POST") && is_path(path, length, MESSAGES_PATH)) {
        return FOREBAY_ANTHROPIC_ROUTE_MESSAGES;
    }
    if (is_get && (is_path(path, length, MODELS_PATH) || is_path(path, length, MODELS_PATH "/"))) {
        return FOREBAY_ANTHROPIC_ROUTE_MODEL_LIST;
    }
    if (is_get && has_prefix(path, length, MODELS_PATH "/")) {
        return FOREBAY_ANTHROPIC_ROUTE_MODEL_LOOKUP;
    }
    return FOREBAY_ANTHROPIC_ROUTE_PASS_THROUGH;
}

static spisor_status fill_route(const forebay_anthropic_route_argument *argument,
                                forebay_anthropic_route_answer *answer) {
    size_t length = strcspn(argument->path, "?");
    uint32_t kind = kind_of(argument->method, argument->path, length);
    if (kind == FOREBAY_ANTHROPIC_ROUTE_MODEL_LOOKUP) {
        size_t id_start = strlen(MODELS_PATH "/");
        char *id = forebay_storage_allocate(&answer->storage, length - id_start + 1);
        if (id == NULL) {
            return SPISOR_OUT_OF_MEMORY;
        }
        memcpy(id, argument->path + id_start, length - id_start);
        answer->model_id = id;
    }
    answer->kind = kind;
    return SPISOR_OK;
}

spisor_status anthropic_route_request(void *context, const forebay_anthropic_route_argument *argument,
                                      forebay_anthropic_route_answer *answer, spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, ARGUMENT_SHAPE) ||
        !SPISOR_IS_EMPTY_ANSWER(answer, ANSWER_SHAPE)) {
        return spisor_refuse_shape(refusal, "anthropic's route_request");
    }
    if (!forebay_is_text(argument->method)) {
        return forebay_refuse_member(refusal, "method", "an HTTP method, such as POST");
    }
    if (argument->path == NULL) {
        return forebay_refuse_member(refusal, "path", "a text");
    }
    spisor_status status = fill_route(argument, answer);
    if (status != SPISOR_OK) {
        forebay_storage_release(answer->storage);
        answer->storage = NULL;
    }
    return status;
}

ANTHROPIC_RELEASE_ANSWER(anthropic_release_route, forebay_anthropic_route_answer, ANSWER_SHAPE)
