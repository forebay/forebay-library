#ifndef ACCOUNTS_MANAGE_H
#define ACCOUNTS_MANAGE_H

#include "forebay/accounts.h"

spisor_status accounts_list(void *context, const forebay_accounts_list_argument *argument,
                            forebay_accounts_list_answer *answer, spisor_refusal *refusal);
spisor_status accounts_upsert(void *context, const forebay_accounts_upsert_argument *argument,
                              forebay_accounts_upsert_answer *answer, spisor_refusal *refusal);
spisor_status accounts_remove(void *context, const forebay_accounts_remove_argument *argument,
                              forebay_accounts_remove_answer *answer, spisor_refusal *refusal);
void accounts_release_list_answer(void *context, forebay_accounts_list_answer *answer);

/** Whether a value is a record whose `id` member is non-empty text, as every stored account is. */
int accounts_is_account_record(const spisor_value *account);

#endif
