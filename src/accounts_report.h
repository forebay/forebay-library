#ifndef ACCOUNTS_REPORT_H
#define ACCOUNTS_REPORT_H

#include "forebay/accounts.h"

spisor_status accounts_report(void *context, const forebay_accounts_report_argument *argument,
                              forebay_accounts_report_answer *answer, spisor_refusal *refusal);

#endif
