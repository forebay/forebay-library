#ifndef ACCOUNTS_SUPPORT_H
#define ACCOUNTS_SUPPORT_H

#include "accounts_store.h"
#include "spisor/status.h"

/**
 * Makes the change to the opened store and fills the answer, setting `*changed` when the store must be written back.
 * OK, or OUT_OF_MEMORY with the answer empty; the store is then not written.
 */
typedef spisor_status (*accounts_change)(accounts_store *store, const void *request, void *answer, int *changed);

/** Empties an answer a change filled, for the write that then failed; NULL for an answer that owns nothing. */
typedef void (*accounts_answer_release)(void *answer);

/**
 * Runs one read-modify-write under a single hold of the store's lock: open, change and fill the answer, write back
 * when changed, close. The answer is filled before the write, so nothing allocates once the store has changed.
 *
 * @return OK; REFUSED for a store that cannot be locked, read or written; OUT_OF_MEMORY. The answer is empty and the
 * store as it was for both.
 */
spisor_status accounts_run_change(const char *path, accounts_change change, const void *request, void *answer,
                                  accounts_answer_release release, spisor_refusal *refusal);

/** The refusal of a store that cannot be locked, read or written, or OUT_OF_MEMORY for a store that ran out. */
spisor_status accounts_refuse_store(accounts_store_status status, spisor_refusal *refusal);

/** The id of an account entry, NULL when the entry is not an object with a non-empty string id. */
const char *accounts_account_id(const cJSON *account);

/** The first account whose id is id, with its index in *index when asked. */
cJSON *accounts_find(const cJSON *accounts, const char *id, int *index);

int accounts_has_lane(const char *lane);

/** Sets a member of object in place, adding it when absent; 0 when memory runs out. */
int accounts_set_number(cJSON *object, const char *name, double value);
int accounts_set_text(cJSON *object, const char *name, const char *value);
int accounts_set_boolean(cJSON *object, const char *name, int value);

/** Replaces a member with item, which is consumed; 0 when memory runs out. */
int accounts_set_item(cJSON *object, const char *name, cJSON *item);

#endif
