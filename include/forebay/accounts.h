#ifndef FOREBAY_PUBLIC_ACCOUNTS_H
#define FOREBAY_PUBLIC_ACCOUNTS_H

/**
 * @file
 * forebay-library's `accounts` service: the pool of accounts a provider plugin rotates through, kept in one JSON
 * store file per provider and changed under that file's lock, so two processes sharing a store never lose a write.
 * A size-prefixed struct is read only up to its `size`. Text the caller hands in is NUL-terminated UTF-8 borrowed for
 * the call; what an answer or a refusal points at is the provider's until the release for its kind.
 *
 * An account and a quota reading are the provider plugin's own records, so they cross as spisor_value records: the
 * store keeps every member it is given, and reads only `id`, `enabled`, `coolingDownUntil`, `cooldownReason`,
 * `disabledReason`, `lastUsed`, `rateLimitResetTimes` and `quota` itself. No member at any depth may be named
 * `access`, `refresh`, `accessToken`, `refreshToken`, `idToken` or `apiKey`: tokens live in the plugin's secrets.
 * Every number is finite; a time is milliseconds since the Unix epoch.
 */

#include <stddef.h>
#include <stdint.h>

#include "forebay/definition.h"
#include "spisor/status.h"
#include "spisor/value.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The store to read: the path of its file, not empty. First shape ends at `store`. */
typedef struct forebay_accounts_list_argument {
    uint32_t size;
    const char *store;
} forebay_accounts_list_argument;

/** Every stored entry that is a record, in store order, each kept as it was stored. First shape ends at `storage`. */
typedef struct forebay_accounts_list_answer {
    uint32_t size;
    const spisor_value *accounts;
    size_t count;
    void *storage;
} forebay_accounts_list_answer;

/** `account` a record whose `id` is non-empty text. First shape ends at `account`. */
typedef struct forebay_accounts_upsert_argument {
    uint32_t size;
    const char *store;
    const spisor_value *account;
} forebay_accounts_upsert_argument;

/** What an upsert did. Append-only: a published value keeps its number. */
typedef enum forebay_accounts_upsert_outcome {
    FOREBAY_ACCOUNTS_ADDED = 1,
    FOREBAY_ACCOUNTS_UNCHANGED = 2,
    FOREBAY_ACCOUNTS_UPDATED = 3
} forebay_accounts_upsert_outcome;

/** A forebay_accounts_upsert_outcome. It owns nothing. First shape ends at `outcome`. */
typedef struct forebay_accounts_upsert_answer {
    uint32_t size;
    uint32_t outcome;
} forebay_accounts_upsert_answer;

/** The account to drop, by its id. First shape ends at `id`. */
typedef struct forebay_accounts_remove_argument {
    uint32_t size;
    const char *store;
    const char *id;
} forebay_accounts_remove_argument;

/** 1 when an account was dropped. It owns nothing. First shape ends at `removed`. */
typedef struct forebay_accounts_remove_answer {
    uint32_t size;
    uint32_t removed;
} forebay_accounts_remove_answer;

/** How acquire picks an account. Append-only; any other number is hybrid, as a strategy the pool does not know. */
typedef enum forebay_accounts_strategy {
    FOREBAY_ACCOUNTS_STICKY = 1,
    FOREBAY_ACCOUNTS_ROUND_ROBIN = 2,
    FOREBAY_ACCOUNTS_HYBRID = 3
} forebay_accounts_strategy;

/**
 * `lane` NULL or empty for none, which uses the pool-wide cursor and ignores lane resets; `strategy` a
 * forebay_accounts_strategy. First shape ends at `now`.
 */
typedef struct forebay_accounts_acquire_argument {
    uint32_t size;
    const char *store;
    const char *lane;
    uint32_t strategy;
    double now;
} forebay_accounts_acquire_argument;

/** What acquire found. Append-only: a published value keeps its number. */
typedef enum forebay_accounts_acquire_kind {
    FOREBAY_ACCOUNTS_CLAIMED = 1,
    FOREBAY_ACCOUNTS_NONE = 2
} forebay_accounts_acquire_kind;

/**
 * A claim names the account `id` and whether it is `available` now; hybrid claims whoever frees soonest while nobody
 * is available, and then `available_at` says when. NONE leaves `id` NULL and says, in `available_at`, the soonest
 * time any account frees, when one ever does. `knows_available_at` is 1 whenever `available_at` holds a time.
 * First shape ends at `storage`.
 */
typedef struct forebay_accounts_acquire_answer {
    uint32_t size;
    uint32_t kind;
    const char *id;
    uint32_t available;
    uint32_t knows_available_at;
    double available_at;
    void *storage;
} forebay_accounts_acquire_answer;

/** How a request on an account went. Append-only: a published value keeps its number. */
typedef enum forebay_accounts_report_outcome {
    FOREBAY_ACCOUNTS_SUCCESS = 1,
    FOREBAY_ACCOUNTS_RATE_LIMITED = 2,
    FOREBAY_ACCOUNTS_ERROR = 3,
    FOREBAY_ACCOUNTS_DISABLED = 4,
    FOREBAY_ACCOUNTS_QUOTA = 5
} forebay_accounts_report_outcome;

/**
 * `outcome` a forebay_accounts_report_outcome; `lane` and `reason` NULL for none. `reset_at` is read when
 * `has_reset_at` is 1, and `random`, the jitter in [0, 1), when `has_random` is 1, else 0.5. An error, and a rate
 * limit without a reset, back off by `base_milliseconds` doubled per `attempt` (a negative attempt counts as 0), up
 * to `max_milliseconds`, both from 0. `quota` is the reading a QUOTA outcome stores, a record, and read for no other.
 * First shape ends at `quota`.
 */
typedef struct forebay_accounts_report_argument {
    uint32_t size;
    const char *store;
    const char *id;
    uint32_t outcome;
    double now;
    const char *lane;
    const char *reason;
    uint32_t has_reset_at;
    double reset_at;
    uint32_t has_random;
    double random;
    double attempt;
    double base_milliseconds;
    double max_milliseconds;
    const spisor_value *quota;
} forebay_accounts_report_argument;

/** 1 when the store changed; an unknown id changes nothing. It owns nothing. First shape ends at `changed`. */
typedef struct forebay_accounts_report_answer {
    uint32_t size;
    uint32_t changed;
} forebay_accounts_report_answer;

/**
 * An access token's expiry, read from the plugin's secrets when `has_expires` is 1, and the skew before it that
 * already counts as expired: 60000 unless `has_skew_milliseconds` is 1. First shape ends at `skew_milliseconds`.
 */
typedef struct forebay_accounts_token_argument {
    uint32_t size;
    uint32_t has_expires;
    double expires;
    double now;
    uint32_t has_skew_milliseconds;
    double skew_milliseconds;
} forebay_accounts_token_argument;

/** 1 when the token has no expiry or expires within the skew of now. First shape ends at `expired`. */
typedef struct forebay_accounts_token_answer {
    uint32_t size;
    uint32_t expired;
} forebay_accounts_token_answer;

/**
 * A quota reading's pools, as the provider parsed them: `count` values at `pools`, of which a record whose
 * `remainingFraction` is a number above zero has capacity, and anything else has none. First shape ends at `count`.
 */
typedef struct forebay_accounts_capacity_argument {
    uint32_t size;
    const spisor_value *pools;
    size_t count;
} forebay_accounts_capacity_argument;

/** 1 when any pool has capacity. It owns nothing. First shape ends at `capacity`. */
typedef struct forebay_accounts_capacity_answer {
    uint32_t size;
    uint32_t capacity;
} forebay_accounts_capacity_answer;

/**
 * The accounts service. Every operation answers MALFORMED, before it allocates or touches the store, for an argument
 * or an answer absent or short of its first shape, an answer that is not empty, an empty or absent store path, a
 * number that is not finite, or a value spisor_value_check refuses; REFUSED, with the refusal saying what to repair,
 * for a store that cannot be locked, read as a JSON object or written, which is then left as it was; and
 * OUT_OF_MEMORY, with the store left as it was and the answer empty. First shape ends at `release_refusal`.
 *
 * @implNote `remove` and not `delete`, so the header stays includable from C++, where `delete` is a keyword.
 */
typedef struct forebay_accounts_definition {
    uint32_t size;
    void *context;
    /** The pool without locking it; a store that is absent or not a JSON object lists as empty. */
    spisor_status (*list)(void *context, const forebay_accounts_list_argument *argument,
                          forebay_accounts_list_answer *answer, spisor_refusal *refusal);
    /**
     * Adds the account, or lays its members over the stored one with that id, keeping the stored order and every
     * member it does not name. A stored account the members leave equal, nested records compared in any key order and
     * lists in order, is UNCHANGED and the store is not written.
     *
     * @return OK; MALFORMED for an account that is not a record with a non-empty text `id`; REFUSED for an account
     * carrying a token member, named in the refusal, before the store is opened.
     */
    spisor_status (*upsert)(void *context, const forebay_accounts_upsert_argument *argument,
                            forebay_accounts_upsert_answer *answer, spisor_refusal *refusal);
    /** Drops the first account with the id, leaving every cursor as it was. MALFORMED for a NULL id. */
    spisor_status (*remove)(void *context, const forebay_accounts_remove_argument *argument,
                            forebay_accounts_remove_answer *answer, spisor_refusal *refusal);
    /**
     * Picks an account by the strategy, moves the cursor it keeps, lane by lane, and records the claim as the
     * account's `lastUsed`. Sticky keeps the cursor's account while it is available; round robin starts after it;
     * hybrid is sticky and, while nobody is available, claims whoever frees soonest.
     */
    spisor_status (*acquire)(void *context, const forebay_accounts_acquire_argument *argument,
                             forebay_accounts_acquire_answer *answer, spisor_refusal *refusal);
    /**
     * Records how a request on the account went: success clears its cooldown; a rate limit sets the lane's reset, or
     * without a lane cools the whole account; an error cools it by the jittered backoff unless a reset on the same
     * lane is still running; disabled turns it off with the reason; quota stores the reading. No cooldown already
     * running is ever shortened.
     *
     * @return OK; MALFORMED for a NULL id, an unknown outcome, a random outside [0, 1), a negative bound, or a QUOTA
     * outcome without a record; REFUSED for a quota carrying a token member, before the store is opened.
     */
    spisor_status (*report)(void *context, const forebay_accounts_report_argument *argument,
                            forebay_accounts_report_answer *answer, spisor_refusal *refusal);
    /** Whether an access token needs refreshing; MALFORMED for a negative skew. Touches no store. */
    spisor_status (*token_expired)(void *context, const forebay_accounts_token_argument *argument,
                                   forebay_accounts_token_answer *answer, spisor_refusal *refusal);
    /** Whether a quota reading has capacity left. MALFORMED for a nonzero count at NULL pools. Touches no store. */
    spisor_status (*has_capacity)(void *context, const forebay_accounts_capacity_argument *argument,
                                  forebay_accounts_capacity_answer *answer, spisor_refusal *refusal);
    void (*release_list_answer)(void *context, forebay_accounts_list_answer *answer);
    void (*release_acquire_answer)(void *context, forebay_accounts_acquire_answer *answer);
    void (*release_refusal)(void *context, spisor_refusal *refusal);
} forebay_accounts_definition;

/** The static forebay_accounts_definition, living until the library is unloaded. */
FOREBAY_DEFINITION_API const void *forebay_library_accounts_definition(void);

#ifdef __cplusplus
}
#endif

#endif
