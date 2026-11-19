#ifndef ACCOUNTS_QUOTA_H
#define ACCOUNTS_QUOTA_H

#include "forebay/accounts.h"

spisor_status accounts_has_capacity(void *context, const forebay_accounts_capacity_argument *argument,
                                    forebay_accounts_capacity_answer *answer, spisor_refusal *refusal);

#endif
