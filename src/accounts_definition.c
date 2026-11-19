#include "forebay/accounts.h"

#include "accounts_manage.h"
#include "accounts_quota.h"
#include "accounts_report.h"
#include "accounts_select.h"
#include "accounts_token.h"
#include "definition_support.h"

static const forebay_accounts_definition definition = {
    sizeof(forebay_accounts_definition), NULL, accounts_list, accounts_upsert, accounts_remove, accounts_acquire,
    accounts_report, accounts_token_expired, accounts_has_capacity, accounts_release_list_answer,
    accounts_release_acquire_answer, spisor_release_refusal,
};

FOREBAY_DEFINITION_API const void *forebay_library_accounts_definition(void) {
    return &definition;
}
