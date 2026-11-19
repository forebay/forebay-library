#ifndef FOREBAY_TEST_ACCOUNTS_CALL_H
#define FOREBAY_TEST_ACCOUNTS_CALL_H

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "forebay/accounts.h"
#include "spisor/json_value.h"
#include "vector_reader.h"

/**
 * @file
 * One step of an accounts vector as a call through the definition: the vector's JSON argument read into the
 * operation's structs, the call made, and its answer or refusal written back as the JSON result the vector expects,
 * so the vectors keep their format and only test code reads them.
 */

typedef enum accounts_operation {
    ACCOUNTS_LIST,
    ACCOUNTS_UPSERT,
    ACCOUNTS_REMOVE,
    ACCOUNTS_ACQUIRE,
    ACCOUNTS_REPORT,
    ACCOUNTS_TOKEN_EXPIRED,
    ACCOUNTS_HAS_CAPACITY,
    ACCOUNTS_UNKNOWN
} accounts_operation;

/** A vector step's call: the structs of its operation and the trees they point into. */
typedef struct accounts_call {
    accounts_operation operation;
    spisor_value_tree value;
    forebay_accounts_list_argument list;
    forebay_accounts_upsert_argument upsert;
    forebay_accounts_remove_argument remove;
    forebay_accounts_acquire_argument acquire;
    forebay_accounts_report_argument report;
    forebay_accounts_token_argument token;
    forebay_accounts_capacity_argument capacity;
} accounts_call;

/** What one call answered, every answer kind beside the refusal. */
typedef struct accounts_answers {
    spisor_status status;
    spisor_refusal refusal;
    forebay_accounts_list_answer list;
    forebay_accounts_upsert_answer upsert;
    forebay_accounts_remove_answer remove;
    forebay_accounts_acquire_answer acquire;
    forebay_accounts_report_answer report;
    forebay_accounts_token_answer token;
    forebay_accounts_capacity_answer capacity;
} accounts_answers;

static inline const forebay_accounts_definition *accounts_definition(void) {
    return forebay_library_accounts_definition();
}

static inline accounts_operation accounts_operation_named(const char *name) {
    static const char *const names[] = {"list", "upsert", "remove", "acquire", "report", "tokenExpired",
                                        "hasCapacity"};
    for (int index = 0; index < ACCOUNTS_UNKNOWN; index++) {
        if (name != NULL && strcmp(names[index], name) == 0) {
            return (accounts_operation)index;
        }
    }
    return ACCOUNTS_UNKNOWN;
}

static inline void accounts_empty_call(accounts_call *call) {
    memset(call, 0, sizeof *call);
    call->operation = ACCOUNTS_UNKNOWN;
    call->value = (spisor_value_tree)SPISOR_INIT(spisor_value_tree);
    call->list = (forebay_accounts_list_argument)SPISOR_INIT(forebay_accounts_list_argument);
    call->upsert = (forebay_accounts_upsert_argument)SPISOR_INIT(forebay_accounts_upsert_argument);
    call->remove = (forebay_accounts_remove_argument)SPISOR_INIT(forebay_accounts_remove_argument);
    call->acquire = (forebay_accounts_acquire_argument)SPISOR_INIT(forebay_accounts_acquire_argument);
    call->report = (forebay_accounts_report_argument)SPISOR_INIT(forebay_accounts_report_argument);
    call->token = (forebay_accounts_token_argument)SPISOR_INIT(forebay_accounts_token_argument);
    call->capacity = (forebay_accounts_capacity_argument)SPISOR_INIT(forebay_accounts_capacity_argument);
}

static inline void accounts_empty_answers(accounts_answers *answers) {
    memset(answers, 0, sizeof *answers);
    answers->refusal.size = (uint32_t)sizeof(spisor_refusal);
    answers->list.size = (uint32_t)sizeof(forebay_accounts_list_answer);
    answers->upsert.size = (uint32_t)sizeof(forebay_accounts_upsert_answer);
    answers->remove.size = (uint32_t)sizeof(forebay_accounts_remove_answer);
    answers->acquire.size = (uint32_t)sizeof(forebay_accounts_acquire_answer);
    answers->report.size = (uint32_t)sizeof(forebay_accounts_report_answer);
    answers->token.size = (uint32_t)sizeof(forebay_accounts_token_answer);
    answers->capacity.size = (uint32_t)sizeof(forebay_accounts_capacity_answer);
}

static inline const char *accounts_optional_text(const cJSON *argument, const char *key) {
    return cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(argument, key));
}

/** A number member into `*number`, answering 1 when the member is one. */
static inline uint32_t accounts_optional_number(const cJSON *argument, const char *key, double *number) {
    const cJSON *member = cJSON_GetObjectItemCaseSensitive(argument, key);
    if (!cJSON_IsNumber(member)) {
        return 0;
    }
    *number = member->valuedouble;
    return 1;
}

static inline uint32_t accounts_strategy_named(const char *name) {
    if (name != NULL && strcmp(name, "sticky") == 0) {
        return FOREBAY_ACCOUNTS_STICKY;
    }
    if (name != NULL && strcmp(name, "round-robin") == 0) {
        return FOREBAY_ACCOUNTS_ROUND_ROBIN;
    }
    return name != NULL && strcmp(name, "hybrid") == 0 ? FOREBAY_ACCOUNTS_HYBRID : 0;
}

static inline uint32_t accounts_outcome_named(const char *name) {
    static const char *const names[] = {"success", "rate-limited", "error", "disabled", "quota"};
    for (uint32_t index = 0; index < 5; index++) {
        if (name != NULL && strcmp(names[index], name) == 0) {
            return FOREBAY_ACCOUNTS_SUCCESS + index;
        }
    }
    return 0;
}

static inline int accounts_read_value(const cJSON *argument, const char *key, accounts_call *call,
                                      const spisor_value **value) {
    if (cJSON_GetObjectItemCaseSensitive(argument, key) == NULL) {
        *value = NULL;
        return 1;
    }
    if (!spisor_vector_value(argument, key, &call->value)) {
        return 0;
    }
    *value = &call->value.root;
    return 1;
}

static inline void accounts_read_report(const cJSON *argument, const char *store,
                                        forebay_accounts_report_argument *report) {
    report->store = store;
    report->id = accounts_optional_text(argument, "id");
    report->outcome = accounts_outcome_named(accounts_optional_text(argument, "outcome"));
    accounts_optional_number(argument, "now", &report->now);
    report->lane = accounts_optional_text(argument, "lane");
    report->reason = accounts_optional_text(argument, "reason");
    report->has_reset_at = accounts_optional_number(argument, "resetAt", &report->reset_at);
    report->has_random = accounts_optional_number(argument, "random", &report->random);
    accounts_optional_number(argument, "attempt", &report->attempt);
    accounts_optional_number(argument, "baseMilliseconds", &report->base_milliseconds);
    accounts_optional_number(argument, "maxMilliseconds", &report->max_milliseconds);
}

/** Reads a vector step's `function` and `argument` into a call on `store`; 0, having said why, for a broken step. */
static inline int accounts_read_call(const cJSON *step, const char *store, accounts_call *call) {
    const cJSON *argument = cJSON_GetObjectItemCaseSensitive(step, "argument");
    const spisor_value *pools = NULL;
    accounts_empty_call(call);
    call->operation = accounts_operation_named(accounts_optional_text(step, "function"));
    if (call->operation == ACCOUNTS_UNKNOWN || !cJSON_IsObject(argument)) {
        fprintf(stderr, "not a vector step: needs a known function and an argument object\n");
        return 0;
    }
    call->list.store = store;
    call->upsert.store = store;
    call->remove.store = store;
    call->remove.id = accounts_optional_text(argument, "id");
    call->acquire.store = store;
    call->acquire.lane = accounts_optional_text(argument, "lane");
    call->acquire.strategy = accounts_strategy_named(accounts_optional_text(argument, "strategy"));
    accounts_optional_number(argument, "now", &call->acquire.now);
    accounts_read_report(argument, store, &call->report);
    call->token.has_expires = accounts_optional_number(argument, "expires", &call->token.expires);
    accounts_optional_number(argument, "now", &call->token.now);
    call->token.has_skew_milliseconds =
        accounts_optional_number(argument, "skewMilliseconds", &call->token.skew_milliseconds);
    if (call->operation == ACCOUNTS_UPSERT) {
        return accounts_read_value(argument, "account", call, &call->upsert.account);
    }
    if (call->operation == ACCOUNTS_REPORT) {
        return accounts_read_value(argument, "quota", call, &call->report.quota);
    }
    if (call->operation == ACCOUNTS_HAS_CAPACITY) {
        if (!accounts_read_value(argument, "pools", call, &pools)) {
            return 0;
        }
        call->capacity.pools = pools == NULL ? NULL : pools->items;
        call->capacity.count = pools == NULL ? 0 : pools->count;
    }
    return 1;
}

static inline void accounts_release_call(accounts_call *call) {
    spisor_value_release(&call->value);
}

/** Makes the call, into answers the caller releases with accounts_release_answers. */
static inline void accounts_make_call(const accounts_call *call, accounts_answers *answers) {
    const forebay_accounts_definition *accounts = accounts_definition();
    void *context = accounts->context;
    accounts_empty_answers(answers);
    switch (call->operation) {
    case ACCOUNTS_LIST:
        answers->status = accounts->list(context, &call->list, &answers->list, &answers->refusal);
        break;
    case ACCOUNTS_UPSERT:
        answers->status = accounts->upsert(context, &call->upsert, &answers->upsert, &answers->refusal);
        break;
    case ACCOUNTS_REMOVE:
        answers->status = accounts->remove(context, &call->remove, &answers->remove, &answers->refusal);
        break;
    case ACCOUNTS_ACQUIRE:
        answers->status = accounts->acquire(context, &call->acquire, &answers->acquire, &answers->refusal);
        break;
    case ACCOUNTS_REPORT:
        answers->status = accounts->report(context, &call->report, &answers->report, &answers->refusal);
        break;
    case ACCOUNTS_TOKEN_EXPIRED:
        answers->status = accounts->token_expired(context, &call->token, &answers->token, &answers->refusal);
        break;
    default:
        answers->status = accounts->has_capacity(context, &call->capacity, &answers->capacity, &answers->refusal);
        break;
    }
}

static inline void accounts_release_answers(accounts_answers *answers) {
    const forebay_accounts_definition *accounts = accounts_definition();
    accounts->release_list_answer(accounts->context, &answers->list);
    accounts->release_acquire_answer(accounts->context, &answers->acquire);
    accounts->release_refusal(accounts->context, &answers->refusal);
}

static inline cJSON *accounts_flag_result(const char *name, uint32_t flag) {
    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, name, flag);
    return result;
}

static inline cJSON *accounts_list_result(const forebay_accounts_list_answer *answer) {
    spisor_value list = {SPISOR_VALUE_LIST, 0, 0, NULL, 0, answer->accounts, NULL, answer->count};
    cJSON *accounts = NULL;
    cJSON *result = cJSON_CreateObject();
    if (spisor_json_of_value(&list, &accounts) != SPISOR_OK) {
        accounts = cJSON_CreateStringReference("an answer that is no well-formed tree");
    }
    cJSON_AddItemToObject(result, "accounts", accounts);
    return result;
}

static inline cJSON *accounts_acquire_result(const forebay_accounts_acquire_answer *answer) {
    cJSON *result = cJSON_CreateObject();
    if (answer->kind == FOREBAY_ACCOUNTS_NONE) {
        cJSON_AddTrueToObject(result, "none");
        if (answer->knows_available_at) {
            cJSON_AddNumberToObject(result, "nextAvailableAt", answer->available_at);
        }
        return result;
    }
    if (answer->kind == FOREBAY_ACCOUNTS_CLAIMED) {
        cJSON_AddStringToObject(result, "id", answer->id == NULL ? "(null)" : answer->id);
        cJSON_AddBoolToObject(result, "available", answer->available);
        if (answer->knows_available_at) {
            cJSON_AddNumberToObject(result, "availableAt", answer->available_at);
        }
    }
    return result;
}

static inline cJSON *accounts_upsert_result(const forebay_accounts_upsert_answer *answer) {
    static const char *const outcomes[] = {"(none)", "added", "unchanged", "updated"};
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "outcome", answer->outcome <= 3 ? outcomes[answer->outcome] : "(unknown)");
    return result;
}

/** The answer as the JSON result a vector writes: a refusal as {"refused", "fix"}, any other failure as its status. */
static inline cJSON *accounts_result_of(const accounts_call *call, const accounts_answers *answers) {
    if (answers->status == SPISOR_REFUSED) {
        cJSON *result = cJSON_CreateObject();
        cJSON_AddStringToObject(result, "refused", answers->refusal.detail == NULL ? "" : answers->refusal.detail);
        cJSON_AddStringToObject(result, "fix", answers->refusal.fix == NULL ? "" : answers->refusal.fix);
        return result;
    }
    if (answers->status != SPISOR_OK) {
        return accounts_flag_result("failedWithStatus", (uint32_t)answers->status);
    }
    switch (call->operation) {
    case ACCOUNTS_LIST:
        return accounts_list_result(&answers->list);
    case ACCOUNTS_UPSERT:
        return accounts_upsert_result(&answers->upsert);
    case ACCOUNTS_REMOVE:
        return accounts_flag_result("removed", answers->remove.removed);
    case ACCOUNTS_ACQUIRE:
        return accounts_acquire_result(&answers->acquire);
    case ACCOUNTS_REPORT:
        return accounts_flag_result("changed", answers->report.changed);
    case ACCOUNTS_TOKEN_EXPIRED:
        return accounts_flag_result("expired", answers->token.expired);
    default:
        return accounts_flag_result("capacity", answers->capacity.capacity);
    }
}

#endif
