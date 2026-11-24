#ifndef ANTHROPIC_RESET_TIME_H
#define ANTHROPIC_RESET_TIME_H

#include <stddef.h>

#include "forebay/anthropic.h"

spisor_status anthropic_reset_time(void *context, const forebay_anthropic_reset_argument *argument,
                                   forebay_anthropic_reset_answer *answer, spisor_refusal *refusal);

/**
 * The reset `reset_time` reads from checked headers and `now`, in `*reset_at` epoch milliseconds; 1 when there is one,
 * 0 with `*reset_at` untouched when there is none.
 */
int anthropic_read_reset(const forebay_pair *headers, size_t header_count, double now, double *reset_at);

#endif
