#include "anthropic_reply_head.h"

#include <string.h>

#include "answer_storage.h"
#include "spisor/definition.h"

#define BODY_FIRST_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_body_answer, storage)
#define BODY_HEAD_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_body_answer, header_count)
#define LOOKUP_FIRST_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_model_lookup_answer, storage)
#define LOOKUP_HEAD_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_model_lookup_answer, header_count)

static const char JSON_TYPE[] = "application/json";
static const char EVENT_STREAM_TYPE[] = "text/event-stream";

static const forebay_pair JSON_HEADERS[] = {{"content-type", JSON_TYPE}};
static const forebay_pair EVENT_STREAM_HEADERS[] = {{"content-type", EVENT_STREAM_TYPE},
                                                    {"cache-control", "no-cache"}};

/** The reply fields both answers name alike. */
typedef struct reply_head {
    const char *content_type;
    const forebay_pair *headers;
    size_t header_count;
} reply_head;

static reply_head head_of(anthropic_media media) {
    if (media == ANTHROPIC_EVENT_STREAM) {
        return (reply_head){EVENT_STREAM_TYPE, EVENT_STREAM_HEADERS,
                            sizeof EVENT_STREAM_HEADERS / sizeof EVENT_STREAM_HEADERS[0]};
    }
    return (reply_head){JSON_TYPE, JSON_HEADERS, sizeof JSON_HEADERS / sizeof JSON_HEADERS[0]};
}

/** The bytes of an answer of `size` to empty: through its size, at most the whole struct of `whole` bytes. */
static size_t cleared_shape(uint32_t size, size_t whole) {
    return size < whole ? size : whole;
}

int anthropic_body_answer_is_empty(const forebay_anthropic_body_answer *answer) {
    if (!SPISOR_IS_EMPTY_COUNTED_ANSWER(answer, BODY_FIRST_SHAPE, answer->length)) {
        return 0;
    }
    return !spisor_has_shape(answer->size, BODY_HEAD_SHAPE) || answer->header_count == 0;
}

int anthropic_lookup_answer_is_empty(const forebay_anthropic_model_lookup_answer *answer) {
    if (!SPISOR_IS_EMPTY_COUNTED_ANSWER(answer, LOOKUP_FIRST_SHAPE, answer->length)) {
        return 0;
    }
    return !spisor_has_shape(answer->size, LOOKUP_HEAD_SHAPE) || answer->header_count == 0;
}

void anthropic_head_body(forebay_anthropic_body_answer *answer, uint32_t status, anthropic_media media) {
    if (!spisor_has_shape(answer->size, BODY_HEAD_SHAPE)) {
        return;
    }
    reply_head head = head_of(media);
    answer->status = status;
    answer->content_type = head.content_type;
    answer->headers = head.headers;
    answer->header_count = head.header_count;
}

void anthropic_head_lookup(forebay_anthropic_model_lookup_answer *answer, uint32_t status) {
    if (!spisor_has_shape(answer->size, LOOKUP_HEAD_SHAPE)) {
        return;
    }
    reply_head head = head_of(ANTHROPIC_JSON);
    answer->status = status;
    answer->content_type = head.content_type;
    answer->headers = head.headers;
    answer->header_count = head.header_count;
}

void anthropic_release_body(void *context, forebay_anthropic_body_answer *answer) {
    (void)context;
    if (answer != NULL && spisor_has_shape(answer->size, BODY_FIRST_SHAPE)) {
        forebay_storage_release(answer->storage);
        SPISOR_CLEAR_ANSWER(answer, cleared_shape(answer->size, sizeof *answer));
    }
}

void anthropic_release_model_lookup(void *context, forebay_anthropic_model_lookup_answer *answer) {
    (void)context;
    if (answer != NULL && spisor_has_shape(answer->size, LOOKUP_FIRST_SHAPE)) {
        forebay_storage_release(answer->storage);
        SPISOR_CLEAR_ANSWER(answer, cleared_shape(answer->size, sizeof *answer));
    }
}
