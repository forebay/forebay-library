#ifndef ACCOUNTS_TOKEN_H
#define ACCOUNTS_TOKEN_H

#include "forebay/accounts.h"

spisor_status accounts_token_expired(void *context, const forebay_accounts_token_argument *argument,
                                     forebay_accounts_token_answer *answer, spisor_refusal *refusal);

#endif
