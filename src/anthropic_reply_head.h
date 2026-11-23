#ifndef ANTHROPIC_REPLY_HEAD_H
#define ANTHROPIC_REPLY_HEAD_H

#include <stdint.h>

#include "forebay/anthropic.h"

/** The media a body is written in, which names its reply's content type and headers. */
typedef enum anthropic_media {
    ANTHROPIC_JSON,
    ANTHROPIC_EVENT_STREAM
} anthropic_media;

/** Whether a body answer can be filled: empty through its first shape and, when it reaches them, of no headers. */
int anthropic_body_answer_is_empty(const forebay_anthropic_body_answer *answer);

/** Whether a lookup answer can be filled, as anthropic_body_answer_is_empty says of a body answer. */
int anthropic_lookup_answer_is_empty(const forebay_anthropic_model_lookup_answer *answer);

/** Sets the reply a filled body goes out in, `status` 0 for none of its own, when the answer reaches it. */
void anthropic_head_body(forebay_anthropic_body_answer *answer, uint32_t status, anthropic_media media);

/** Sets the reply a filled lookup goes out in, when the answer reaches it. */
void anthropic_head_lookup(forebay_anthropic_model_lookup_answer *answer, uint32_t status);

/** Empties a body answer through its size, NULL or one short of its first shape doing nothing. */
void anthropic_release_body(void *context, forebay_anthropic_body_answer *answer);

/** Empties a lookup answer through its size, NULL or one short of its first shape doing nothing. */
void anthropic_release_model_lookup(void *context, forebay_anthropic_model_lookup_answer *answer);

#endif
