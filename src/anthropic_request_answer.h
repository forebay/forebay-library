#ifndef ANTHROPIC_REQUEST_ANSWER_H
#define ANTHROPIC_REQUEST_ANSWER_H

#include "forebay/anthropic.h"

/**
 * What decode_request answers besides the request: the forwarded headers and the invalid-request reply, which an
 * answer holds only when its size reaches them.
 */

/** Whether the answer is present, reaches its first shape and holds nothing. */
int anthropic_request_answer_is_empty(const forebay_anthropic_request_answer *answer);

/** MALFORMED, naming `headers`, for headers the argument reaches that are not a list of named pairs. */
spisor_status anthropic_check_forwarded_headers(const forebay_anthropic_body_argument *argument,
                                                spisor_refusal *refusal);

/** Fills the answer's forwarded headers from the argument's when both reach them; false when memory runs out. */
spisor_status anthropic_answer_forwarded_headers(const forebay_anthropic_body_argument *argument,
                                                 forebay_anthropic_request_answer *answer);

/** Why a request body is invalid, as the reply's message says. */
typedef enum anthropic_invalid_body {
    ANTHROPIC_UNREADABLE_BODY,
    ANTHROPIC_NO_OBJECT_BODY
} anthropic_invalid_body;

/** Answers an invalid body with the invalid-request reply, or MALFORMED to an answer too short to hold it. */
spisor_status anthropic_answer_invalid_request(forebay_anthropic_request_answer *answer, anthropic_invalid_body why,
                                               spisor_refusal *refusal);

void anthropic_release_request(void *context, forebay_anthropic_request_answer *answer);

#endif
