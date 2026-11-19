#include "accounts_token.h"

#include "definition_support.h"

#define DEFAULT_SKEW_MILLISECONDS 60000.0
#define TOKEN_SHAPE FOREBAY_SHAPE_THROUGH(forebay_accounts_token_argument, skew_milliseconds)
#define TOKEN_ANSWER_SHAPE FOREBAY_SHAPE_THROUGH(forebay_accounts_token_answer, expired)

/**
 * TokenRefresh.accessTokenExpired for an expiry the provider read from its secrets: no expiry is expired,
 * and so is one within skew of now (<=, so the edge counts as expired).
 */
static int is_expired(const forebay_accounts_token_argument *argument, double skew) {
    return !argument->has_expires || argument->expires <= argument->now + skew;
}

spisor_status accounts_token_expired(void *context, const forebay_accounts_token_argument *argument,
                                     forebay_accounts_token_answer *answer, spisor_refusal *refusal) {
    (void)context;
    if (!FOREBAY_HAS_SHAPE(argument, TOKEN_SHAPE) || !FOREBAY_HAS_SHAPE(answer, TOKEN_ANSWER_SHAPE)) {
        return forebay_refuse_shape(refusal, "accounts' token_expired");
    }
    if (!forebay_is_finite(argument->expires) || !forebay_is_finite(argument->now) ||
        !forebay_is_finite(argument->skew_milliseconds)) {
        return forebay_refuse_member(refusal, "every number", "finite");
    }
    double skew = argument->has_skew_milliseconds ? argument->skew_milliseconds : DEFAULT_SKEW_MILLISECONDS;
    if (skew < 0) {
        return forebay_refuse_member(refusal, "skew_milliseconds", "a number of milliseconds from 0");
    }
    answer->expired = (uint32_t)is_expired(argument, skew);
    return SPISOR_OK;
}
