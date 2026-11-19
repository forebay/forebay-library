#include "accounts_report.h"

#include "accounts_availability.h"
#include "accounts_backoff.h"
#include "accounts_secrets.h"
#include "accounts_support.h"
#include "definition_support.h"

#define REPORT_SHAPE FOREBAY_SHAPE_THROUGH(forebay_accounts_report_argument, quota)
#define REPORT_ANSWER_SHAPE FOREBAY_SHAPE_THROUGH(forebay_accounts_report_answer, changed)

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
    return accounts_set_number(account, "coolingDownUntil", until) &&
           accounts_set_text(account, "cooldownReason", reason);
}

/** AccountManager.reportSuccess: clears the cooldown and marks the account just used. */
static int apply_success(cJSON *account, const report_request *request, int *changed) {
    *changed = 1;
    cJSON_DeleteItemFromObjectCaseSensitive(account, "cooldownReason");
    return accounts_set_number(account, "coolingDownUntil", 0) &&
           accounts_set_number(account, "lastUsed", request->now);
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

static spisor_status record(accounts_store *store, const void *given, void *filled, int *changed) {
    const report_request *request = given;
    forebay_accounts_report_answer *answer = filled;
    cJSON *account = accounts_find(accounts_store_accounts(store), request->id, NULL);
    int applied = 0;
    if (account != NULL && !apply(account, request, &applied)) {
        return SPISOR_OUT_OF_MEMORY;
    }
    answer->changed = (uint32_t)applied;
    *changed = applied;
    return SPISOR_OK;
}

static void clear_report_answer(void *filled) {
    forebay_accounts_report_answer *answer = filled;
    answer->changed = 0;
}

static int is_outcome(uint32_t outcome) {
    return outcome >= FOREBAY_ACCOUNTS_SUCCESS && outcome <= FOREBAY_ACCOUNTS_QUOTA;
}

static int needs_backoff(const forebay_accounts_report_argument *argument) {
    return argument->outcome == FOREBAY_ACCOUNTS_ERROR ||
           (argument->outcome == FOREBAY_ACCOUNTS_RATE_LIMITED && !argument->has_reset_at);
}

static int has_finite_numbers(const forebay_accounts_report_argument *argument) {
    return forebay_is_finite(argument->now) && forebay_is_finite(argument->reset_at) &&
           forebay_is_finite(argument->random) && forebay_is_finite(argument->attempt) &&
           forebay_is_finite(argument->base_milliseconds) && forebay_is_finite(argument->max_milliseconds);
}

static spisor_status check_numbers(const forebay_accounts_report_argument *argument, spisor_refusal *refusal) {
    if (!has_finite_numbers(argument)) {
        return forebay_refuse_member(refusal, "every number", "finite");
    }
    if (argument->has_random && (argument->random < 0 || argument->random >= 1)) {
        return forebay_refuse_member(refusal, "random", "a jitter in [0, 1), as Math.random() answers");
    }
    if (needs_backoff(argument) && (argument->base_milliseconds < 0 || argument->max_milliseconds < 0)) {
        return forebay_refuse_member(refusal, "base_milliseconds and max_milliseconds", "milliseconds from 0");
    }
    return SPISOR_OK;
}

static spisor_status check_argument(const forebay_accounts_report_argument *argument, spisor_refusal *refusal) {
    if (!forebay_is_text(argument->store)) {
        return forebay_refuse_member(refusal, "store", "the path of the accounts file");
    }
    if (argument->id == NULL) {
        return forebay_refuse_member(refusal, "id", "the id of an account");
    }
    if (!is_outcome(argument->outcome)) {
        return forebay_refuse_member(refusal, "outcome", "a forebay_accounts_report_outcome");
    }
    if (argument->outcome == FOREBAY_ACCOUNTS_QUOTA &&
        (argument->quota == NULL || argument->quota->kind != SPISOR_VALUE_RECORD)) {
        return forebay_refuse_member(refusal, "quota", "a record, as a QUOTA outcome stores it");
    }
    return check_numbers(argument, refusal);
}

static report_request request_of(const forebay_accounts_report_argument *argument, const cJSON *quota) {
    report_request request = {
        argument->id,
        argument->lane,
        argument->reason,
        (report_outcome)(argument->outcome - FOREBAY_ACCOUNTS_SUCCESS),
        argument->now,
        argument->has_random ? argument->random : DEFAULT_JITTER,
        argument->attempt,
        argument->base_milliseconds,
        argument->max_milliseconds,
        argument->has_reset_at != 0,
        argument->reset_at,
        quota,
    };
    return request;
}

static spisor_status report_with(const forebay_accounts_report_argument *argument, const cJSON *quota,
                                 forebay_accounts_report_answer *answer, spisor_refusal *refusal) {
    report_request request = request_of(argument, quota);
    return accounts_run_change(argument->store, record, &request, answer, clear_report_answer, refusal);
}

spisor_status accounts_report(void *context, const forebay_accounts_report_argument *argument,
                              forebay_accounts_report_answer *answer, spisor_refusal *refusal) {
    cJSON *quota = NULL;
    (void)context;
    if (!FOREBAY_HAS_SHAPE(argument, REPORT_SHAPE) || !FOREBAY_HAS_SHAPE(answer, REPORT_ANSWER_SHAPE)) {
        return forebay_refuse_shape(refusal, "accounts' report");
    }
    spisor_status status = check_argument(argument, refusal);
    if (status == SPISOR_OK && argument->outcome == FOREBAY_ACCOUNTS_QUOTA) {
        status = accounts_refuse_credentials(argument->quota, "quota", refusal);
    }
    if (status == SPISOR_OK && argument->outcome == FOREBAY_ACCOUNTS_QUOTA) {
        status = forebay_json_of_member(argument->quota, "quota", &quota, refusal);
    }
    if (status == SPISOR_OK) {
        status = report_with(argument, quota, answer, refusal);
    }
    forebay_delete_overwritten(quota);
    return status;
}
