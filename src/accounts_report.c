#include "accounts_report.h"

#include <string.h>

#include "accounts_availability.h"
#include "accounts_backoff.h"
#include "accounts_secrets.h"
#include "accounts_support.h"

#define DEFAULT_JITTER 0.5
#define DEFAULT_ERROR_REASON "transient error"
#define DEFAULT_DISABLED_REASON "disabled"
#define RATE_LIMITED_REASON "rate limited"

typedef enum report_outcome {
    OUTCOME_SUCCESS,
    OUTCOME_RATE_LIMITED,
    OUTCOME_ERROR,
    OUTCOME_DISABLED,
    OUTCOME_QUOTA
} report_outcome;

typedef struct report_request {
    const char *store;
    const char *id;
    const char *lane;
    const char *reason;
    report_outcome outcome;
    double now;
    double random;
    double attempt;
    double base;
    double maximum;
    int has_reset;
    double reset_at;
    const cJSON *quota;
} report_request;

static double backoff_for(const report_request *request) {
    return accounts_backoff_milliseconds(request->attempt, request->base, request->maximum, request->random);
}

static int set_cooldown(cJSON *account, double until, const char *reason) {
    return accounts_set_number(account, "coolingDownUntil", until) && accounts_set_text(account, "cooldownReason", reason);
}

/** AccountManager.reportSuccess: clears the cooldown and marks the account just used. */
static int apply_success(cJSON *account, const report_request *request, int *changed) {
    *changed = 1;
    cJSON_DeleteItemFromObjectCaseSensitive(account, "cooldownReason");
    return accounts_set_number(account, "coolingDownUntil", 0) && accounts_set_number(account, "lastUsed", request->now);
}

/** Whether a cooldown still running at now already lasts until until or longer. */
static int running_cooldown_covers(const cJSON *account, double until, double now) {
    const cJSON *running = cJSON_GetObjectItemCaseSensitive(account, "coolingDownUntil");
    return cJSON_IsNumber(running) && running->valuedouble > now && running->valuedouble >= until;
}

/**
 * AccountManager.reportError: a cooldown by jittered backoff, unless a reset the provider supplied for this
 * very lane is still active, which owns usable-again on its own. A reset on another lane never suppresses it.
 * A cooldown already running to a later time, such as a lane-less rate limit, is never shortened.
 */
static int apply_error(cJSON *account, const report_request *request, int *changed) {
    if (accounts_is_lane_rate_limited(account, request->lane, request->now)) {
        return 1;
    }
    double until = request->now + backoff_for(request);
    if (running_cooldown_covers(account, until, request->now)) {
        return 1;
    }
    *changed = 1;
    const char *reason = request->reason != NULL ? request->reason : DEFAULT_ERROR_REASON;
    return set_cooldown(account, until, reason);
}

static int set_lane_reset(cJSON *account, const char *lane, double reset_at) {
    cJSON *resets = cJSON_GetObjectItemCaseSensitive(account, "rateLimitResetTimes");
    if (!cJSON_IsObject(resets)) {
        resets = cJSON_CreateObject();
        if (!accounts_set_item(account, "rateLimitResetTimes", resets)) {
            return 0;
        }
    }
    return accounts_set_number(resets, lane, reset_at);
}

/**
 * AccountManager.reportRateLimit: the lane is limited until the reset the response named, or until the
 * backoff when it named none. Selection reads a lane reset only for a named lane, so a limit with no lane
 * becomes an account-wide cooldown that never shortens one already running.
 */
static int apply_rate_limited(cJSON *account, const report_request *request, int *changed) {
    double reset_at = request->has_reset ? request->reset_at : request->now + backoff_for(request);
    if (accounts_has_lane(request->lane)) {
        *changed = 1;
        return set_lane_reset(account, request->lane, reset_at);
    }
    if (running_cooldown_covers(account, reset_at, request->now)) {
        return 1;
    }
    *changed = 1;
    return set_cooldown(account, reset_at, RATE_LIMITED_REASON);
}

static int apply_disabled(cJSON *account, const report_request *request, int *changed) {
    *changed = 1;
    const char *reason = request->reason != NULL ? request->reason : DEFAULT_DISABLED_REASON;
    return accounts_set_boolean(account, "enabled", 0) && accounts_set_text(account, "disabledReason", reason);
}

static int apply_quota(cJSON *account, const report_request *request, int *changed) {
    *changed = 1;
    return accounts_set_item(account, "quota", cJSON_Duplicate(request->quota, 1));
}

static int apply(cJSON *account, const report_request *request, int *changed) {
    switch (request->outcome) {
    case OUTCOME_SUCCESS:
        return apply_success(account, request, changed);
    case OUTCOME_RATE_LIMITED:
        return apply_rate_limited(account, request, changed);
    case OUTCOME_ERROR:
        return apply_error(account, request, changed);
    case OUTCOME_DISABLED:
        return apply_disabled(account, request, changed);
    default:
        return apply_quota(account, request, changed);
    }
}

static cJSON *changed_answer(int changed) {
    cJSON *answer = cJSON_CreateObject();
    if (answer == NULL || cJSON_AddBoolToObject(answer, "changed", changed) == NULL) {
        cJSON_Delete(answer);
        return NULL;
    }
    return answer;
}

static void record(accounts_store *store, const void *given, accounts_outcome *outcome) {
    const report_request *request = given;
    cJSON *account = accounts_find(accounts_store_accounts(store), request->id, NULL);
    int changed = 0;
    if (account != NULL && !apply(account, request, &changed)) {
        return;
    }
    outcome->result = changed_answer(changed);
    outcome->changed = changed;
}

static int parse_outcome(const char *name, report_outcome *outcome) {
    static const char *const names[] = {"success", "rate-limited", "error", "disabled", "quota"};
    for (int index = 0; index < 5; index++) {
        if (strcmp(names[index], name) == 0) {
            *outcome = (report_outcome)index;
            return 1;
        }
    }
    return 0;
}

static int read_outcome(const spisor_call *call, report_request *request, uint8_t **out, size_t *out_len,
                        int32_t *status) {
    const cJSON *name = NULL;
    if (!service_require(call, "outcome", service_is_string, "a string", &name, out, out_len, status)) {
        return 0;
    }
    if (!parse_outcome(name->valuestring, &request->outcome)) {
        *status = service_fail("outcome", "is not one of the five outcomes",
                               "send success, rate-limited, error, disabled or quota", out, out_len);
        return 0;
    }
    return 1;
}

static int read_random(const spisor_call *call, report_request *request, uint8_t **out, size_t *out_len,
                       int32_t *status) {
    int present = 0;
    request->random = DEFAULT_JITTER;
    if (!accounts_optional_number(call, "random", &request->random, &present, out, out_len, status)) {
        return 0;
    }
    if (request->random < 0 || request->random >= 1) {
        *status = service_fail("random", "is outside [0, 1)", "send random from Math.random()", out, out_len);
        return 0;
    }
    return 1;
}

static int read_millisecond_bound(const spisor_call *call, const char *name, double *value, uint8_t **out,
                                  size_t *out_len, int32_t *status) {
    const cJSON *member = NULL;
    if (!service_require(call, name, service_is_number, "a number", &member, out, out_len, status)) {
        return 0;
    }
    if (member->valuedouble < 0) {
        *status = service_fail(name, "is negative", "send a number of milliseconds from 0", out, out_len);
        return 0;
    }
    *value = member->valuedouble;
    return 1;
}

static int needs_backoff(const report_request *request) {
    return request->outcome == OUTCOME_ERROR || (request->outcome == OUTCOME_RATE_LIMITED && !request->has_reset);
}

static int read_backoff(const spisor_call *call, report_request *request, uint8_t **out, size_t *out_len,
                        int32_t *status) {
    int present = 0;
    return read_random(call, request, out, out_len, status) &&
           accounts_optional_number(call, "attempt", &request->attempt, &present, out, out_len, status) &&
           (!needs_backoff(request) ||
            (read_millisecond_bound(call, "baseMilliseconds", &request->base, out, out_len, status) &&
             read_millisecond_bound(call, "maxMilliseconds", &request->maximum, out, out_len, status)));
}

static int read_quota(const spisor_call *call, report_request *request, uint8_t **out, size_t *out_len,
                      int32_t *status) {
    return request->outcome != OUTCOME_QUOTA ||
           service_require(call, "quota", service_is_object, "an object", &request->quota, out, out_len, status);
}

static int read_request(const spisor_call *call, report_request *request, uint8_t **out, size_t *out_len,
                        int32_t *status) {
    const cJSON *id = NULL;
    const cJSON *now = NULL;
    memset(request, 0, sizeof(*request));
    if (accounts_require_store(call, &request->store, out, out_len, status) &&
        service_require(call, "id", service_is_string, "a string", &id, out, out_len, status) &&
        read_outcome(call, request, out, out_len, status) &&
        service_require(call, "now", service_is_number, "a number", &now, out, out_len, status) &&
        accounts_optional_text(call, "lane", &request->lane, out, out_len, status) &&
        accounts_optional_text(call, "reason", &request->reason, out, out_len, status) &&
        accounts_optional_number(call, "resetAt", &request->reset_at, &request->has_reset, out, out_len, status) &&
        read_backoff(call, request, out, out_len, status) && read_quota(call, request, out, out_len, status)) {
        request->id = id->valuestring;
        request->now = now->valuedouble;
        return 1;
    }
    return 0;
}

SERVICE_EXPORT int32_t forebay_library_accounts_report(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len) {
    spisor_call call;
    int32_t status = service_open(in, in_len, &call, out, out_len);
    if (status != SPISOR_STATUS_SUCCESS) {
        return status;
    }
    report_request request;
    if (read_request(&call, &request, out, out_len, &status)) {
        const char *token_member = request.quota == NULL ? NULL : accounts_find_token_member(request.quota);
        status = token_member != NULL ? accounts_refuse_token_member("quota", token_member, out, out_len)
                                      : accounts_run_change(request.store, record, &request, out, out_len);
    }
    spisor_discard_call(&call);
    return status;
}
