#ifndef ACCOUNTS_AVAILABILITY_H
#define ACCOUNTS_AVAILABILITY_H

#include "cJSON.h"

/** RateLimitMath.isEnabled: an account is enabled unless it says enabled: false. */
int accounts_is_enabled(const cJSON *account);

/** RateLimitMath.isCoolingDown: the cooldown deadline lies strictly after now. */
int accounts_is_cooling_down(const cJSON *account, double now);

/** RateLimitMath.isLaneRateLimited: lane's reset lies strictly after now; no lane limits nothing. */
int accounts_is_lane_rate_limited(const cJSON *account, const char *lane, double now);

/** RateLimitMath.isAvailable; an entry that is not an account with an id is never available. */
int accounts_is_available(const cJSON *account, const char *lane, double now);

/**
 * RateLimitMath.availableAt: the soonest time the account is usable for lane, never before now, and
 * infinity for a disabled account or an entry that is not an account.
 */
double accounts_available_at(const cJSON *account, const char *lane, double now);

/** AccountManager.nextAvailableAt: the soonest finite availableAt over the pool; 0 with *found clear when none. */
double accounts_next_available_at(const cJSON *accounts, const char *lane, double now, int *found);

#endif
