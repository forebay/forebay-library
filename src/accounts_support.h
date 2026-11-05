#ifndef ACCOUNTS_SUPPORT_H
#define ACCOUNTS_SUPPORT_H

#include "accounts_store.h"
#include "service_support.h"

/** What one change to the store decided; result is owned and NULL with refused clear means memory ran out. */
typedef struct accounts_outcome {
    cJSON *result;
    int changed;
    int refused;
    char cause[200];
    const char *fix;
} accounts_outcome;

/** Makes the change to the opened store; the store is written back only when outcome->changed is set. */
typedef void (*accounts_change)(accounts_store *store, const void *request, accounts_outcome *outcome);

/**
 * Runs one read-modify-write under a single hold of the store's lock: open, change, write back when
 * changed, close, answer. A store that cannot be locked, read or written answers a refusal.
 */
int32_t accounts_run_change(const char *path, accounts_change change, const void *request, uint8_t **out,
                            size_t *out_len);

/** Records a semantic refusal in outcome. */
void accounts_refuse(accounts_outcome *outcome, const char *cause, const char *fix);

/** The store argument, a non-empty path; answers 0 with the call already failed. */
int accounts_require_store(const spisor_call *call, const char **path, uint8_t **out, size_t *out_len,
                           int32_t *status);

/** Reads an optional member of the type asked for, leaving *member NULL when it is absent or null. */
int accounts_optional(const spisor_call *call, const char *name, int (*is_type)(const cJSON *),
                      const char *type_name, const cJSON **member, uint8_t **out, size_t *out_len, int32_t *status);

/** An optional text member as its string, NULL when absent. */
int accounts_optional_text(const spisor_call *call, const char *name, const char **text, uint8_t **out,
                           size_t *out_len, int32_t *status);

/** An optional number member into *number, with *present telling whether it was given. */
int accounts_optional_number(const spisor_call *call, const char *name, double *number, int *present,
                             uint8_t **out, size_t *out_len, int32_t *status);

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
