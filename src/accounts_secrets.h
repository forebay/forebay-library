#ifndef ACCOUNTS_SECRETS_H
#define ACCOUNTS_SECRETS_H

#include "cJSON.h"

/**
 * The name of the first member, at any depth, that would hold a credential: access, refresh, or any
 * name containing token, ignoring case. NULL when there is none; the name belongs to the tree.
 *
 * @implNote Tokens live in the plugin's secrets, never in the account store, so the store refuses
 * the account instead of trusting every caller to strip them. expires is not a secret and passes.
 */
const char *accounts_find_token_member(const cJSON *value);

#endif
