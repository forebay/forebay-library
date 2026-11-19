#include <string.h>

#include "anthropic_decode.h"
#include "anthropic_encode.h"
#include "anthropic_operations.h"
#include "anthropic_stringify.h"
#include "forebay/anthropic.h"
#include "spisor/allocation.h"
#include "spisor/definition.h"
#include "spisor/json_kind.h"

#define BODY_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_body_argument, length)
#define REQUEST_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_request_answer, storage)
#define ENCODE_REQUEST_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_encode_request_argument, request)
#define RESPONSE_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_response_answer, storage)
#define ENCODE_RESPONSE_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_encode_response_argument, response)

static spisor_status refuse_not_json(spisor_refusal *refusal) {
    static const char detail[] =
        "the body is not JSON this library can read: it must be UTF-8 JSON text "
        "in which no string holds a NUL (\\u0000)";
    return spisor_refuse_as(refusal, SPISOR_MALFORMED, detail, "send a JSON body without a NUL in any string");
}

typedef const void *(*body_reader)(anthropic_reader *reader, const cJSON *root);

static const void *read_request(anthropic_reader *reader, const cJSON *root) {
    return anthropic_decode_request(reader, root);
}

static const void *read_response(anthropic_reader *reader, const cJSON *root) {
    return anthropic_decode_response(reader, root);
}

/**
 * Parses the body and reads it with `read`; `*result` and `*storage` are set only when the status is OK.
 * @implNote Shape checks stay with each operation, since each names its own answer.
 */
static spisor_status decode_body(const forebay_anthropic_body_argument *argument, body_reader read, const void **result,
                                 void **storage, spisor_refusal *refusal) {
    if (argument->bytes == NULL && argument->length > 0) {
        return anthropic_refuse_bytes(refusal);
    }
    static const uint8_t no_bytes[1] = {0};
    int out_of_memory = 0;
    cJSON *root = spisor_parse_json_reporting(argument->bytes == NULL ? no_bytes : argument->bytes,
                                              argument->length, &out_of_memory);
    if (root == NULL) {
        return out_of_memory ? SPISOR_OUT_OF_MEMORY : refuse_not_json(refusal);
    }
    void *chain = NULL;
    anthropic_reader reader = {&chain, 0};
    const void *decoded = read(&reader, root);
    cJSON_Delete(root);
    if (decoded == NULL) {
        forebay_storage_release(chain);
        return SPISOR_OUT_OF_MEMORY;
    }
    *result = decoded;
    *storage = chain;
    return SPISOR_OK;
}

static spisor_status decode_request(void *context, const forebay_anthropic_body_argument *argument,
                                    forebay_anthropic_request_answer *answer, spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, BODY_SHAPE) ||
        !SPISOR_IS_EMPTY_ANSWER(answer, REQUEST_ANSWER_SHAPE)) {
        return spisor_refuse_shape(refusal, "anthropic's decode_request");
    }
    const void *request = NULL;
    spisor_status status = decode_body(argument, read_request, &request, &answer->storage, refusal);
    answer->request = status == SPISOR_OK ? request : NULL;
    return status;
}

static spisor_status decode_response(void *context, const forebay_anthropic_body_argument *argument,
                                     forebay_anthropic_response_answer *answer, spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, BODY_SHAPE) ||
        !SPISOR_IS_EMPTY_ANSWER(answer, RESPONSE_ANSWER_SHAPE)) {
        return spisor_refuse_shape(refusal, "anthropic's decode_response");
    }
    const void *response = NULL;
    spisor_status status = decode_body(argument, read_response, &response, &answer->storage, refusal);
    answer->response = status == SPISOR_OK ? response : NULL;
    return status;
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

/** Fills the answer from a built wire object, which it deletes, or refuses what the writer found malformed. */
static spisor_status finish_encoding(const anthropic_writer *writer, cJSON *wire, anthropic_problem *problem,
                                     forebay_anthropic_body_answer *answer, spisor_refusal *refusal) {
    if (writer->status == SPISOR_MALFORMED) {
        cJSON_Delete(wire);
        anthropic_writer_problem(writer, problem);
        return anthropic_refuse_problem(refusal, problem);
    }
    if (writer->status != SPISOR_OK) {
        cJSON_Delete(wire);
        return writer->status;
    }
    return write_body(wire, answer);
}

static spisor_status encode_request(void *context, const forebay_anthropic_encode_request_argument *argument,
                                    forebay_anthropic_body_answer *answer, spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, ENCODE_REQUEST_SHAPE) ||
        !SPISOR_IS_EMPTY_ANSWER(answer, ANTHROPIC_BODY_ANSWER_SHAPE)) {
        return spisor_refuse_shape(refusal, "anthropic's encode_request");
    }
    anthropic_problem problem;
    spisor_status checked = anthropic_check_request(argument->request, &problem);
    if (checked == SPISOR_MALFORMED) {
        return anthropic_refuse_problem(refusal, &problem);
    }
    if (checked != SPISOR_OK) {
        return checked;
    }
    anthropic_writer writer = {SPISOR_OK, NULL};
    cJSON *wire = anthropic_encode_request_object(&writer, argument->request);
    return finish_encoding(&writer, wire, &problem, answer, refusal);
}

static spisor_status encode_response(void *context, const forebay_anthropic_encode_response_argument *argument,
                                     forebay_anthropic_body_answer *answer, spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, ENCODE_RESPONSE_SHAPE) ||
        !SPISOR_IS_EMPTY_ANSWER(answer, ANTHROPIC_BODY_ANSWER_SHAPE)) {
        return spisor_refuse_shape(refusal, "anthropic's encode_response");
    }
    anthropic_problem problem;
    spisor_status checked = anthropic_check_response(argument->response, &problem);
    if (checked == SPISOR_MALFORMED) {
        return anthropic_refuse_problem(refusal, &problem);
    }
    if (checked != SPISOR_OK) {
        return checked;
    }
    anthropic_writer writer = {SPISOR_OK, NULL};
    cJSON *wire = anthropic_encode_response_object(&writer, argument->response);
    return finish_encoding(&writer, wire, &problem, answer, refusal);
}

static ANTHROPIC_RELEASE_ANSWER(release_request, forebay_anthropic_request_answer, REQUEST_ANSWER_SHAPE)
static ANTHROPIC_RELEASE_ANSWER(release_response, forebay_anthropic_response_answer, RESPONSE_ANSWER_SHAPE)
ANTHROPIC_RELEASE_ANSWER(anthropic_release_body, forebay_anthropic_body_answer, ANTHROPIC_BODY_ANSWER_SHAPE)

static const forebay_anthropic_definition definition = {
    sizeof(forebay_anthropic_definition),
    NULL,
    decode_request,
    encode_request,
    release_request,
    anthropic_release_body,
    spisor_release_refusal,
    decode_response,
    encode_response,
    release_response,
    anthropic_stream_decoder_open,
    anthropic_stream_decode,
    anthropic_stream_decode_finish,
    anthropic_release_events,
    anthropic_release_stream_decoder,
    anthropic_stream_encoder_open,
    anthropic_stream_encode,
    anthropic_stream_encode_finish,
    anthropic_release_stream_encoder,
};

FOREBAY_DEFINITION_API const void *forebay_anthropic_definition_of(void) {
    return &definition;
}
