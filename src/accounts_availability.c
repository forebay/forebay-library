#include "accounts_availability.h"

#include <math.h>

#include "accounts_support.h"

static double number_or(const cJSON *object, const char *name, double fallback) {
    const cJSON *member = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsNumber(member) ? member->valuedouble : fallback;
}

static const cJSON *lane_reset(const cJSON *account, const char *lane) {
    if (!accounts_has_lane(lane)) {
        return NULL;
    }
    const cJSON *resets = cJSON_GetObjectItemCaseSensitive(account, "rateLimitResetTimes");
    const cJSON *reset = cJSON_GetObjectItemCaseSensitive(resets, lane);
    return cJSON_IsNumber(reset) ? reset : NULL;
}

int accounts_is_enabled(const cJSON *account) {
    return !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(account, "enabled"));
}

int accounts_is_cooling_down(const cJSON *account, double now) {
    return number_or(account, "coolingDownUntil", -INFINITY) > now;
}

int accounts_is_lane_rate_limited(const cJSON *account, const char *lane, double now) {
    const cJSON *reset = lane_reset(account, lane);
    return reset != NULL && reset->valuedouble > now;
}

int accounts_is_available(const cJSON *account, const char *lane, double now) {
    return accounts_account_id(account) != NULL && accounts_is_enabled(account) &&
           !accounts_is_cooling_down(account, now) && !accounts_is_lane_rate_limited(account, lane, now);
}

double accounts_available_at(const cJSON *account, const char *lane, double now) {
    if (accounts_account_id(account) == NULL || !accounts_is_enabled(account)) {
        return INFINITY;
    }
    double soonest = fmax(0, number_or(account, "coolingDownUntil", 0));
    const cJSON *reset = lane_reset(account, lane);
    if (reset != NULL) {
        soonest = fmax(soonest, reset->valuedouble);
    }
    return fmax(soonest, now);
}

double accounts_next_available_at(const cJSON *accounts, const char *lane, double now, int *found) {
    double best = INFINITY;
    const cJSON *account = NULL;
    cJSON_ArrayForEach(account, accounts) {
        best = fmin(best, accounts_available_at(account, lane, now));
    }
    *found = isfinite(best);
    return *found ? best : 0;
}
