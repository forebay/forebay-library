#ifndef ACCOUNTS_SELECT_H
#define ACCOUNTS_SELECT_H

#include "forebay/accounts.h"

spisor_status accounts_acquire(void *context, const forebay_accounts_acquire_argument *argument,
                               forebay_accounts_acquire_answer *answer, spisor_refusal *refusal);
void accounts_release_acquire_answer(void *context, forebay_accounts_acquire_answer *answer);

#endif
