#ifndef ACCOUNTS_SECRETS_H
#define ACCOUNTS_SECRETS_H

#include "cJSON.h"
#include "service_support.h"

/**
 * The name of the first member, at any depth, that would hold a credential: exactly access, refresh,
 * accessToken, refreshToken, idToken or apiKey, case-sensitively. NULL when there is none; the name
 * belongs to the tree.
 *
 * @implNote Tokens live in the plugin secrets, never in the account store, so the store refuses the
 * value instead of trusting every caller to strip them. The list is exact because a quota reading
 * legitimately has members such as tokensRemaining and inputTokens, and expires is not a secret.
 */
const char *accounts_find_token_member(const cJSON *value);

/** Answers the refusal for a value (an account, a quota) that carries a credential member. */
int32_t accounts_refuse_token_member(const char *subject, const char *member, uint8_t **out, size_t *out_len);

#endif
