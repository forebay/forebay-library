#include "accounts_select.h"

#include <math.h>
#include <string.h>

#include "accounts_availability.h"
#include "accounts_support.h"

#define NO_ACCOUNT (-1)
#define OUT_OF_MEMORY (-2)

typedef enum accounts_strategy { STRATEGY_STICKY, STRATEGY_ROUND_ROBIN, STRATEGY_HYBRID } accounts_strategy;

typedef struct acquire_request {
    const char *store;
    const char *lane;
    accounts_strategy strategy;
    double now;
} acquire_request;

/** An unknown name is hybrid, as Strategy.java reads a missing one. */
static accounts_strategy parse_strategy(const char *name) {
    if (strcmp(name, "round-robin") == 0) {
        return STRATEGY_ROUND_ROBIN;
    }
    return strcmp(name, "sticky") == 0 ? STRATEGY_STICKY : STRATEGY_HYBRID;
}

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

static cJSON *none_answer(const cJSON *accounts, const acquire_request *request) {
    cJSON *answer = cJSON_CreateObject();
    int found = 0;
    double soonest = accounts_next_available_at(accounts, request->lane, request->now, &found);
    if (answer == NULL || cJSON_AddTrueToObject(answer, "none") == NULL ||
        (found && cJSON_AddNumberToObject(answer, "nextAvailableAt", soonest) == NULL)) {
        cJSON_Delete(answer);
        return NULL;
    }
    return answer;
}

static cJSON *claimed_answer(const char *id) {
    cJSON *answer = cJSON_CreateObject();
    if (answer == NULL || cJSON_AddStringToObject(answer, "id", id) == NULL) {
        cJSON_Delete(answer);
        return NULL;
    }
    return answer;
}

static void claim(accounts_store *store, const void *given, accounts_outcome *outcome) {
    const acquire_request *request = given;
    int index = select_index(store->document, request);
    cJSON *accounts = accounts_store_accounts(store);
    if (index == OUT_OF_MEMORY) {
        return;
    }
    if (index == NO_ACCOUNT) {
        outcome->result = none_answer(accounts, request);
        return;
    }
    cJSON *account = cJSON_GetArrayItem(accounts, index);
    if (accounts_set_number(account, "lastUsed", request->now)) {
        outcome->result = claimed_answer(accounts_account_id(account));
        outcome->changed = 1;
    }
}

static int read_request(const spisor_call *call, acquire_request *request, uint8_t **out, size_t *out_len,
                        int32_t *status) {
    const cJSON *strategy = NULL;
    const cJSON *now = NULL;
    memset(request, 0, sizeof(*request));
    if (accounts_require_store(call, &request->store, out, out_len, status) &&
        accounts_optional_text(call, "lane", &request->lane, out, out_len, status) &&
        service_require(call, "strategy", service_is_string, "a string", &strategy, out, out_len, status) &&
        service_require(call, "now", service_is_number, "a number", &now, out, out_len, status)) {
        request->strategy = parse_strategy(strategy->valuestring);
        request->now = now->valuedouble;
        return 1;
    }
    return 0;
}

SERVICE_EXPORT int32_t accounts_acquire(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len) {
    spisor_call call;
    int32_t status = service_open(in, in_len, &call, out, out_len);
    if (status != SPISOR_STATUS_SUCCESS) {
        return status;
    }
    acquire_request request;
    if (read_request(&call, &request, out, out_len, &status)) {
        status = accounts_run_change(request.store, claim, &request, out, out_len);
    }
    spisor_discard_call(&call);
    return status;
}
