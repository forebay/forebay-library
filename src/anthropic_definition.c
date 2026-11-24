#include <string.h>

#include "anthropic_decode.h"
#include "anthropic_encode.h"
#include "anthropic_operations.h"
#include "anthropic_rate_limit_reply.h"
#include "anthropic_request_answer.h"
#include "anthropic_route.h"
#include "forebay/anthropic.h"
#include "spisor/allocation.h"
#include "spisor/definition.h"
#include "spisor/json_kind.h"

#define BODY_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_body_argument, length)
#define ENCODE_REQUEST_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_encode_request_argument, request)
#define RESPONSE_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_response_answer, storage)
#define ENCODE_RESPONSE_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_encode_response_argument, response)
#define OK_STATUS 200u
#define NO_STATUS 0u

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

/** The JSON a body holds, in `*root`; NULL unless OK. SPISOR_NONE, with no refusal, for bytes that are not JSON. */
static spisor_status parse_body(const forebay_anthropic_body_argument *argument, cJSON **root,
                                spisor_refusal *refusal) {
    if (argument->bytes == NULL && argument->length > 0) {
        return anthropic_refuse_bytes(refusal);
    }
    static const uint8_t no_bytes[1] = {0};
    int out_of_memory = 0;
    *root = spisor_parse_json_reporting(argument->bytes == NULL ? no_bytes : argument->bytes, argument->length,
                                        &out_of_memory);
    if (*root == NULL) {
        return out_of_memory ? SPISOR_OUT_OF_MEMORY : SPISOR_NONE;
    }
    return SPISOR_OK;
}

/**
 * Reads a parsed body with `read`; `*result` and `*storage` are set only when the status is OK.
 * @implNote Shape checks stay with each operation, since each names its own answer.
 */
static spisor_status read_parsed(const cJSON *root, body_reader read, const void **result, void **storage) {
    void *chain = NULL;
    anthropic_reader reader = {&chain, 0};
    const void *decoded = read(&reader, root);
    if (decoded == NULL) {
        forebay_storage_release(chain);
        return SPISOR_OUT_OF_MEMORY;
    }
    *result = decoded;
    *storage = chain;
    return SPISOR_OK;
}

static spisor_status decode_body(const forebay_anthropic_body_argument *argument, body_reader read, const void **result,
                                 void **storage, spisor_refusal *refusal) {
    cJSON *root = NULL;
    spisor_status status = parse_body(argument, &root, refusal);
    if (status == SPISOR_OK) {
        status = read_parsed(root, read, result, storage);
    }
    cJSON_Delete(root);
    return status == SPISOR_NONE ? refuse_not_json(refusal) : status;
}

static spisor_status read_request_body(const forebay_anthropic_body_argument *argument, const cJSON *root,
                                       forebay_anthropic_request_answer *answer) {
    const void *request = NULL;
    spisor_status status = read_parsed(root, read_request, &request, &answer->storage);
    if (status == SPISOR_OK) {
        status = anthropic_answer_forwarded_headers(argument, answer);
    }
    if (status != SPISOR_OK) {
        forebay_storage_release(answer->storage);
        answer->storage = NULL;
    }
    answer->request = status == SPISOR_OK ? request : NULL;
    return status;
}

static spisor_status decode_request(void *context, const forebay_anthropic_body_argument *argument,
                                    forebay_anthropic_request_answer *answer, spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, BODY_SHAPE) ||
        !anthropic_request_answer_is_empty(answer)) {
        return spisor_refuse_shape(refusal, "anthropic's decode_request");
    }
    spisor_status headers_checked = anthropic_check_forwarded_headers(argument, refusal);
    cJSON *root = NULL;
    spisor_status status = headers_checked == SPISOR_OK ? parse_body(argument, &root, refusal) : headers_checked;
    if (status == SPISOR_NONE) {
        status = anthropic_answer_invalid_request(answer, ANTHROPIC_UNREADABLE_BODY, refusal);
    } else if (status == SPISOR_OK) {
        status = cJSON_IsObject(root) ? read_request_body(argument, root, answer)
                                      : anthropic_answer_invalid_request(answer, ANTHROPIC_NO_OBJECT_BODY, refusal);
    }
    cJSON_Delete(root);
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

static spisor_status encode_request(void *context, const forebay_anthropic_encode_request_argument *argument,
                                    forebay_anthropic_body_answer *answer, spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, ENCODE_REQUEST_SHAPE) ||
        !anthropic_body_answer_is_empty(answer)) {
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
    return anthropic_finish_reply(&writer, wire, NO_STATUS, answer, refusal);
}

static spisor_status encode_response(void *context, const forebay_anthropic_encode_response_argument *argument,
                                     forebay_anthropic_body_answer *answer, spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, ENCODE_RESPONSE_SHAPE) ||
        !anthropic_body_answer_is_empty(answer)) {
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
    return anthropic_finish_reply(&writer, wire, OK_STATUS, answer, refusal);
}

static ANTHROPIC_RELEASE_ANSWER(release_response, forebay_anthropic_response_answer, RESPONSE_ANSWER_SHAPE)

static const forebay_anthropic_definition definition = {
    sizeof(forebay_anthropic_definition),
    NULL,
    decode_request,
    encode_request,
    decode_response,
    encode_response,
    anthropic_stream_decoder_open,
    anthropic_stream_decode,
    anthropic_stream_decode_finish,
    anthropic_stream_encoder_open,
    anthropic_stream_encode,
    anthropic_stream_encode_finish,
    anthropic_error_envelope,
    anthropic_model_listing,
    anthropic_model_lookup,
    anthropic_rate_limit_reply,
    anthropic_release_request,
    anthropic_release_body,
    release_response,
    anthropic_release_events,
    anthropic_release_stream_decoder,
    anthropic_release_stream_encoder,
    anthropic_release_model_lookup,
    anthropic_release_reply,
    spisor_release_refusal,
    anthropic_route_request,
    anthropic_release_route,
};

FOREBAY_DEFINITION_API const void *forebay_anthropic_definition_of(void) {
    return &definition;
}
