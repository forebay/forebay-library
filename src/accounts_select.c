#include "accounts_select.h"

#include <math.h>

#include "accounts_availability.h"
#include "accounts_support.h"
#include "definition_support.h"
#include "spisor/allocation.h"

#define ACQUIRE_SHAPE SPISOR_SHAPE_THROUGH(forebay_accounts_acquire_argument, now)
#define ACQUIRE_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_accounts_acquire_answer, storage)

#define NO_ACCOUNT (-1)
#define OUT_OF_MEMORY (-2)

typedef enum accounts_strategy { STRATEGY_STICKY, STRATEGY_ROUND_ROBIN, STRATEGY_HYBRID } accounts_strategy;

typedef struct acquire_request {
    const char *lane;
    accounts_strategy strategy;
    double now;
} acquire_request;

/** The lane's cursor when it has one, else the pool-wide one; a cursor outside the pool reads as 0. */
static int cursor_of(const cJSON *document, const char *lane, int count) {
    const cJSON *raw = NULL;
    if (accounts_has_lane(lane)) {
        raw = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(document, "activeIndexByLane"), lane);
    }
    if (!cJSON_IsNumber(raw)) {
        raw = cJSON_GetObjectItemCaseSensitive(document, "activeIndex");
    }
    double cursor = cJSON_IsNumber(raw) ? raw->valuedouble : 0;
    return (cursor < 0 || cursor >= count) ? 0 : (int)cursor;
}

static int set_cursor(cJSON *document, const char *lane, int index) {
    if (accounts_has_lane(lane)) {
        return accounts_set_number(cJSON_GetObjectItemCaseSensitive(document, "activeIndexByLane"), lane, index);
    }
    return accounts_set_number(document, "activeIndex", index);
}

static int first_available_from(const cJSON *accounts, int start, const char *lane, double now) {
    int count = cJSON_GetArraySize(accounts);
    for (int step = 0; step < count; step++) {
        int index = (start + step) % count;
        if (accounts_is_available(cJSON_GetArrayItem(accounts, index), lane, now)) {
            return index;
        }
    }
    return NO_ACCOUNT;
}

/** The hybrid last resort: whoever frees up soonest, the first of equals, nobody when all are disabled. */
static int soonest_free(const cJSON *accounts, const char *lane, double now) {
    int best = NO_ACCOUNT;
    double best_at = INFINITY;
    int count = cJSON_GetArraySize(accounts);
    for (int index = 0; index < count; index++) {
        double at = accounts_available_at(cJSON_GetArrayItem(accounts, index), lane, now);
        if (at < best_at) {
            best_at = at;
            best = index;
        }
    }
    return best;
}

static int move_cursor_to(cJSON *document, const char *lane, int index) {
    return index < 0 || set_cursor(document, lane, index) ? index : OUT_OF_MEMORY;
}

static int select_round_robin(cJSON *document, const cJSON *accounts, const acquire_request *request, int cursor) {
    int count = cJSON_GetArraySize(accounts);
    int index = first_available_from(accounts, (cursor + 1) % count, request->lane, request->now);
    return move_cursor_to(document, request->lane, index);
}

static int select_sticky_or_hybrid(cJSON *document, const cJSON *accounts, const acquire_request *request,
                                   int cursor) {
    if (accounts_is_available(cJSON_GetArrayItem(accounts, cursor), request->lane, request->now)) {
        return cursor;
    }
    int index = first_available_from(accounts, cursor, request->lane, request->now);
    if (index < 0 && request->strategy == STRATEGY_HYBRID) {
        index = soonest_free(accounts, request->lane, request->now);
    }
    return move_cursor_to(document, request->lane, index);
}

/** Selection.selectIndex over the pool document, moving the cursor it keeps. */
static int select_index(cJSON *document, const acquire_request *request) {
    const cJSON *accounts = cJSON_GetObjectItemCaseSensitive(document, "accounts");
    int count = cJSON_GetArraySize(accounts);
    if (count == 0) {
        return NO_ACCOUNT;
    }
    int cursor = cursor_of(document, request->lane, count);
    if (request->strategy == STRATEGY_ROUND_ROBIN) {
        return select_round_robin(document, accounts, request, cursor);
    }
    return select_sticky_or_hybrid(document, accounts, request, cursor);
}

static void fill_none(const cJSON *accounts, const acquire_request *request, forebay_accounts_acquire_answer *answer) {
    int found = 0;
    double soonest = accounts_next_available_at(accounts, request->lane, request->now, &found);
    answer->kind = FOREBAY_ACCOUNTS_NONE;
    answer->knows_available_at = (uint32_t)found;
    answer->available_at = soonest;
}

/**
 * The claimed id, whether the account is available now, and when it will be when it is not: hybrid claims
 * the soonest-free account while every account is cooling, and the provider answers 503 from available.
 */
static spisor_status fill_claimed(const cJSON *account, const acquire_request *request,
                                  forebay_accounts_acquire_answer *answer) {
    char *id = spisor_memory_duplicate_text(accounts_account_id(account));
    int available = accounts_is_available(account, request->lane, request->now);
    if (id == NULL) {
        return SPISOR_OUT_OF_MEMORY;
    }
    answer->kind = FOREBAY_ACCOUNTS_CLAIMED;
    answer->id = id;
    answer->storage = id;
    answer->available = (uint32_t)available;
    answer->knows_available_at = (uint32_t)!available;
    answer->available_at = available ? 0 : accounts_available_at(account, request->lane, request->now);
    return SPISOR_OK;
}

static spisor_status claim(accounts_store *store, const void *given, void *filled, int *changed) {
    const acquire_request *request = given;
    forebay_accounts_acquire_answer *answer = filled;
    int index = select_index(store->document, request);
    cJSON *accounts = accounts_store_accounts(store);
    if (index == OUT_OF_MEMORY) {
        return SPISOR_OUT_OF_MEMORY;
    }
    if (index == NO_ACCOUNT) {
        fill_none(accounts, request, answer);
        return SPISOR_OK;
    }
    cJSON *account = cJSON_GetArrayItem(accounts, index);
    if (!accounts_set_number(account, "lastUsed", request->now)) {
        return SPISOR_OUT_OF_MEMORY;
    }
    *changed = 1;
    return fill_claimed(account, request, answer);
}

static void release_acquire(void *filled) {
    accounts_release_acquire_answer(NULL, filled);
}

void accounts_release_acquire_answer(void *context, forebay_accounts_acquire_answer *answer) {
    (void)context;
    if (answer == NULL || !spisor_has_shape(answer->size, ACQUIRE_ANSWER_SHAPE)) {
        return;
    }
    spisor_memory_release(answer->storage);
    SPISOR_CLEAR_ANSWER(answer, ACQUIRE_ANSWER_SHAPE);
}

static accounts_strategy strategy_of(uint32_t strategy) {
    if (strategy == FOREBAY_ACCOUNTS_STICKY) {
        return STRATEGY_STICKY;
    }
    return strategy == FOREBAY_ACCOUNTS_ROUND_ROBIN ? STRATEGY_ROUND_ROBIN : STRATEGY_HYBRID;
}

spisor_status accounts_acquire(void *context, const forebay_accounts_acquire_argument *argument,
                               forebay_accounts_acquire_answer *answer, spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, ACQUIRE_SHAPE) ||
        !SPISOR_IS_EMPTY_ANSWER(answer, ACQUIRE_ANSWER_SHAPE)) {
        return spisor_refuse_shape(refusal, "accounts' acquire");
    }
    if (!forebay_is_text(argument->store)) {
        return forebay_refuse_member(refusal, "store", "the path of the accounts file");
    }
    if (!forebay_is_finite(argument->now)) {
        return forebay_refuse_member(refusal, "now", "a finite number of milliseconds");
    }
    acquire_request request = {argument->lane, strategy_of(argument->strategy), argument->now};
    return accounts_run_change(argument->store, claim, &request, answer, release_acquire, refusal);
}
