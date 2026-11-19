#include <stdio.h>
#include <string.h>

#include "anthropic_check.h"
#include "anthropic_decode.h"
#include "anthropic_encode.h"
#include "anthropic_stringify.h"
#include "answer_storage.h"
#include "forebay/anthropic.h"
#include "spisor/allocation.h"
#include "spisor/definition.h"
#include "spisor/json_kind.h"

#define BODY_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_body_argument, length)
#define REQUEST_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_request_answer, storage)
#define ENCODE_REQUEST_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_encode_request_argument, request)
#define BODY_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_body_answer, storage)

#define DETAIL_LIMIT 200

static spisor_status refuse_not_json(spisor_refusal *refusal) {
    return spisor_refuse_as(refusal, SPISOR_MALFORMED, "the request body is not JSON: it must be UTF-8 JSON text",
                            "send a JSON body");
}

static spisor_status refuse_problem(spisor_refusal *refusal, const anthropic_problem *problem) {
    char detail[DETAIL_LIMIT];
    char fix[DETAIL_LIMIT];
    snprintf(detail, sizeof detail, "%s must be %s", problem->member, problem->rule);
    snprintf(fix, sizeof fix, "correct %s in the IR request so that it is %s", problem->member, problem->rule);
    return spisor_refuse_as(refusal, SPISOR_MALFORMED, detail, fix);
}

static spisor_status refuse_bytes(spisor_refusal *refusal) {
    return spisor_refuse_as(refusal, SPISOR_MALFORMED, "bytes is NULL beside a length that is not 0",
                            "set bytes to the body, or length to 0");
}

static spisor_status decode_request(void *context, const forebay_anthropic_body_argument *argument,
                                    forebay_anthropic_request_answer *answer, spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, BODY_SHAPE) ||
        !SPISOR_IS_EMPTY_ANSWER(answer, REQUEST_ANSWER_SHAPE)) {
        return spisor_refuse_shape(refusal, "anthropic's decode_request");
    }
    if (argument->bytes == NULL && argument->length > 0) {
        return refuse_bytes(refusal);
    }
    static const uint8_t no_bytes[1] = {0};
    int out_of_memory = 0;
    cJSON *root = spisor_parse_json_reporting(argument->bytes == NULL ? no_bytes : argument->bytes,
                                              argument->length, &out_of_memory);
    if (root == NULL) {
        return out_of_memory ? SPISOR_OUT_OF_MEMORY : refuse_not_json(refusal);
    }
    void *storage = NULL;
    anthropic_reader reader = {&storage, 0};
    const forebay_ir_request *request = anthropic_decode_request(&reader, root);
    cJSON_Delete(root);
    if (request == NULL) {
        forebay_storage_release(storage);
        return SPISOR_OUT_OF_MEMORY;
    }
    answer->request = request;
    answer->storage = storage;
    return SPISOR_OK;
}

/** Fills the body from the wire object, which it deletes; the answer stays empty unless the status is OK. */
static spisor_status write_body(cJSON *wire, forebay_anthropic_body_answer *answer) {
    size_t length = 0;
    char *text = anthropic_stringify(wire, &length);
    cJSON_Delete(wire);
    if (text == NULL) {
        return SPISOR_OUT_OF_MEMORY;
    }
    void *storage = NULL;
    char *body = forebay_storage_allocate(&storage, length + 1);
    if (body != NULL) {
        memcpy(body, text, length + 1);
        answer->bytes = (const uint8_t *)body;
        answer->length = length;
        answer->storage = storage;
    }
    spisor_memory_release(text);
    return body == NULL ? SPISOR_OUT_OF_MEMORY : SPISOR_OK;
}

static spisor_status encode_request(void *context, const forebay_anthropic_encode_request_argument *argument,
                                    forebay_anthropic_body_answer *answer, spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, ENCODE_REQUEST_SHAPE) ||
        !SPISOR_IS_EMPTY_ANSWER(answer, BODY_ANSWER_SHAPE)) {
        return spisor_refuse_shape(refusal, "anthropic's encode_request");
    }
    anthropic_problem problem;
    if (!anthropic_check_request(argument->request, &problem)) {
        return refuse_problem(refusal, &problem);
    }
    anthropic_writer writer = {SPISOR_OK, NULL};
    cJSON *wire = anthropic_encode_request_object(&writer, argument->request);
    if (writer.status == SPISOR_MALFORMED) {
        cJSON_Delete(wire);
        problem.member = writer.member;
        problem.rule = "a well-formed spisor_value";
        return refuse_problem(refusal, &problem);
    }
    if (writer.status != SPISOR_OK) {
        cJSON_Delete(wire);
        return writer.status;
    }
    return write_body(wire, answer);
}

#define RELEASE_ANSWER(name, type, shape)                              \
    static void name(void *context, type *answer) {                    \
        (void)context;                                                 \
        if (answer != NULL && spisor_has_shape(answer->size, shape)) { \
            forebay_storage_release(answer->storage);                  \
            SPISOR_CLEAR_ANSWER(answer, shape);                        \
        }                                                              \
    }

RELEASE_ANSWER(release_request, forebay_anthropic_request_answer, REQUEST_ANSWER_SHAPE)
RELEASE_ANSWER(release_body, forebay_anthropic_body_answer, BODY_ANSWER_SHAPE)

static const forebay_anthropic_definition definition = {
    sizeof(forebay_anthropic_definition),
    NULL,
    decode_request,
    encode_request,
    release_request,
    release_body,
    spisor_release_refusal,
};

FOREBAY_DEFINITION_API const void *forebay_anthropic_definition_of(void) {
    return &definition;
}
