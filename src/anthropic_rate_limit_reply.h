#ifndef ANTHROPIC_RATE_LIMIT_REPLY_H
#define ANTHROPIC_RATE_LIMIT_REPLY_H

#include "forebay/anthropic.h"

spisor_status anthropic_rate_limit_reply(void *context, const forebay_anthropic_rate_limit_argument *argument,
                                         forebay_anthropic_reply_answer *answer, spisor_refusal *refusal);
void anthropic_release_reply(void *context, forebay_anthropic_reply_answer *answer);

#endif
