#include "anthropic_request_answer.h"

#include "anthropic_forwarded_headers.h"
#include "answer_storage.h"
#include "definition_support.h"
#include "forebay/routing.h"
#include "spisor/definition.h"

#define FIRST_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_request_answer, storage)
#define FULL_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_request_answer, message)
#define HEADERS_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_body_argument, header_count)

#define INVALID_REQUEST_STATUS 400u

static const char INVALID_REQUEST_MESSAGE[] =
    "the request body is not a valid Messages API request: the request body must be a JSON object";

static int reaches_the_reply(const forebay_anthropic_request_answer *answer) {
    return spisor_has_shape(answer->size, FULL_ANSWER_SHAPE);
}

static int reaches_the_headers(const forebay_anthropic_body_argument *argument) {
    return spisor_has_shape(argument->size, HEADERS_SHAPE);
}

int anthropic_request_answer_is_empty(const forebay_anthropic_request_answer *answer) {
    if (answer != NULL && reaches_the_reply(answer)) {
        return SPISOR_IS_EMPTY_COUNTED_ANSWER(answer, FULL_ANSWER_SHAPE, answer->forwarded_header_count);
    }
    return SPISOR_IS_EMPTY_ANSWER(answer, FIRST_ANSWER_SHAPE);
}

static int is_named_pairs(const forebay_pair *pairs, size_t count) {
    for (size_t index = 0; index < count; index++) {
        if (pairs[index].name == NULL || pairs[index].value == NULL) {
            return 0;
        }
    }
    return 1;
}

spisor_status anthropic_check_forwarded_headers(const forebay_anthropic_body_argument *argument,
                                                spisor_refusal *refusal) {
    if (!reaches_the_headers(argument)) {
        return SPISOR_OK;
    }
    if (argument->headers == NULL && argument->header_count > 0) {
        return forebay_refuse_member(refusal, "headers", "a pointer to header_count pairs");
    }
    if (!is_named_pairs(argument->headers, argument->header_count)) {
        return forebay_refuse_member(refusal, "headers", "pairs that each carry a name and a value");
    }
    return SPISOR_OK;
}

spisor_status anthropic_answer_forwarded_headers(const forebay_anthropic_body_argument *argument,
                                                 forebay_anthropic_request_answer *answer) {
    if (!reaches_the_headers(argument) || !reaches_the_reply(answer)) {
        return SPISOR_OK;
    }
    return anthropic_forward_headers(&answer->storage, argument->headers, argument->header_count,
                                     &answer->forwarded_headers, &answer->forwarded_header_count)
               ? SPISOR_OK
               : SPISOR_OUT_OF_MEMORY;
}

spisor_status anthropic_answer_invalid_request(forebay_anthropic_request_answer *answer, spisor_refusal *refusal) {
    if (!reaches_the_reply(answer)) {
        return spisor_refuse_as(refusal, SPISOR_MALFORMED, "the body is not a JSON object",
                                "send a JSON object, and an answer that reaches reply_kind, status and message");
    }
    answer->reply_kind = FOREBAY_REPLY_INVALID_REQUEST;
    answer->status = INVALID_REQUEST_STATUS;
    answer->message = INVALID_REQUEST_MESSAGE;
    return SPISOR_OK;
}

void anthropic_release_request(void *context, forebay_anthropic_request_answer *answer) {
    (void)context;
    if (answer == NULL || !spisor_has_shape(answer->size, FIRST_ANSWER_SHAPE)) {
        return;
    }
    forebay_storage_release(answer->storage);
    size_t shape = reaches_the_reply(answer) ? FULL_ANSWER_SHAPE : FIRST_ANSWER_SHAPE;
    SPISOR_CLEAR_ANSWER(answer, shape);
}
