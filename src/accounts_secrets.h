#ifndef ACCOUNTS_SECRETS_H
#define ACCOUNTS_SECRETS_H

#include "spisor/status.h"
#include "spisor/value.h"

/**
 * OK for a well-formed value (an account, a quota) carrying no credential member at any depth: exactly access,
 * refresh, accessToken, refreshToken, idToken or apiKey, case-sensitively. REFUSED naming the member;
 * MALFORMED, refused as `subject`, for a value spisor_value_check refuses; OUT_OF_MEMORY.
 *
 * @implNote Tokens live in the plugin secrets, never in the account store, so the store refuses the
 * value instead of trusting every caller to strip them. The list is exact because a quota reading
 * legitimately has members such as tokensRemaining and inputTokens, and expires is not a secret. Read from the
 * caller's value before anything converts it, so a refused call never copies a token.
 */
spisor_status accounts_refuse_credentials(const spisor_value *value, const char *subject, spisor_refusal *refusal);

#endif
