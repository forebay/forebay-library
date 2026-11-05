#include "accounts_support.h"

#include <stdio.h>
#include <string.h>

#define STORE_NOT_JSON_CAUSE "the account store is not valid JSON"
#define STORE_NOT_JSON_FIX "repair or delete the account store file, then retry"

void accounts_refuse(accounts_outcome *outcome, const char *cause, const char *fix) {
    cJSON_Delete(outcome->result);
    outcome->result = NULL;
    outcome->changed = 0;
    outcome->refused = 1;
    snprintf(outcome->cause, sizeof(outcome->cause), "%s", cause);
    outcome->fix = fix;
}

static void refuse_for(accounts_store_status status, accounts_outcome *outcome) {
    if (status == ACCOUNTS_STORE_UNLOCKABLE) {
        accounts_refuse(outcome, "the account store could not be locked",
                        "check that the store's folder and its .lock file can be created, opened and locked");
    } else if (status == ACCOUNTS_STORE_UNREADABLE) {
        accounts_refuse(outcome, STORE_NOT_JSON_CAUSE, STORE_NOT_JSON_FIX);
    } else if (status == ACCOUNTS_STORE_UNWRITABLE) {
        accounts_refuse(outcome, "the account store could not be written",
                        "check that the store's folder is writable and has room");
    }
}

static int32_t answer_outcome(const accounts_outcome *outcome, uint8_t **out, size_t *out_len) {
    if (outcome->refused) {
        return service_refuse(outcome->cause, outcome->fix, out, out_len);
    }
    return service_answer(outcome->result, out, out_len);
}

int32_t accounts_run_change(const char *path, accounts_change change, const void *request, uint8_t **out,
                            size_t *out_len) {
    accounts_outcome outcome;
    memset(&outcome, 0, sizeof(outcome));
    accounts_store store;
    accounts_store_status status = accounts_store_open(path, 1, &store);
    if (status == ACCOUNTS_STORE_READY) {
        change(&store, request, &outcome);
        if (outcome.changed && !outcome.refused && outcome.result != NULL) {
            status = accounts_store_save(&store);
        }
        accounts_store_close(&store);
    }
    if (status == ACCOUNTS_STORE_OUT_OF_MEMORY) {
        cJSON_Delete(outcome.result);
        return service_out_of_memory(out, out_len);
    }
    refuse_for(status, &outcome);
    return answer_outcome(&outcome, out, out_len);
}

int accounts_require_store(const spisor_call *call, const char **path, uint8_t **out, size_t *out_len,
                           int32_t *status) {
    const cJSON *member = NULL;
    if (!service_require(call, "store", service_is_string, "a string", &member, out, out_len, status)) {
        return 0;
    }
    if (member->valuestring[0] == '\0') {
        *status = service_fail("store", "is empty", "send store as the path of the accounts file", out, out_len);
        return 0;
    }
    *path = member->valuestring;
    return 1;
}

int accounts_optional(const spisor_call *call, const char *name, int (*is_type)(const cJSON *),
                      const char *type_name, const cJSON **member, uint8_t **out, size_t *out_len, int32_t *status) {
    const cJSON *given = cJSON_GetObjectItemCaseSensitive(call->argument, name);
    *member = NULL;
    if (given == NULL || cJSON_IsNull(given)) {
        return 1;
    }
    if (is_type(given)) {
        *member = given;
        return 1;
    }
    char problem[96];
    char fix[128];
    snprintf(problem, sizeof(problem), "is not %s", type_name);
    snprintf(fix, sizeof(fix), "send %s as %s or leave it out", name, type_name);
    *status = service_fail(name, problem, fix, out, out_len);
    return 0;
}

int accounts_optional_text(const spisor_call *call, const char *name, const char **text, uint8_t **out,
                           size_t *out_len, int32_t *status) {
    const cJSON *member = NULL;
    if (!accounts_optional(call, name, service_is_string, "a string", &member, out, out_len, status)) {
        return 0;
    }
    *text = member == NULL ? NULL : member->valuestring;
    return 1;
}

int accounts_optional_number(const spisor_call *call, const char *name, double *number, int *present,
                             uint8_t **out, size_t *out_len, int32_t *status) {
    const cJSON *member = NULL;
    if (!accounts_optional(call, name, service_is_number, "a number", &member, out, out_len, status)) {
        return 0;
    }
    *present = member != NULL;
    if (member != NULL) {
        *number = member->valuedouble;
    }
    return 1;
}

const char *accounts_account_id(const cJSON *account) {
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(account, "id");
    return cJSON_IsObject(account) && cJSON_IsString(id) && id->valuestring[0] != '\0' ? id->valuestring : NULL;
}

cJSON *accounts_find(const cJSON *accounts, const char *id, int *index) {
    int position = 0;
    const cJSON *account = NULL;
    cJSON_ArrayForEach(account, accounts) {
        const char *candidate = accounts_account_id(account);
        if (candidate != NULL && strcmp(candidate, id) == 0) {
            if (index != NULL) {
                *index = position;
            }
            return (cJSON *)account;
        }
        position++;
    }
    return NULL;
}

int accounts_has_lane(const char *lane) {
    return lane != NULL && lane[0] != '\0';
}

int accounts_set_item(cJSON *object, const char *name, cJSON *item) {
    if (item == NULL) {
        return 0;
    }
    if (cJSON_GetObjectItemCaseSensitive(object, name) != NULL) {
        if (!cJSON_ReplaceItemInObjectCaseSensitive(object, name, item)) {
            cJSON_Delete(item);
            return 0;
        }
        return 1;
    }
    return service_add(object, name, item);
}

int accounts_set_number(cJSON *object, const char *name, double value) {
    return accounts_set_item(object, name, cJSON_CreateNumber(value));
}

int accounts_set_text(cJSON *object, const char *name, const char *value) {
    return accounts_set_item(object, name, cJSON_CreateString(value));
}

int accounts_set_boolean(cJSON *object, const char *name, int value) {
    return accounts_set_item(object, name, cJSON_CreateBool(value));
}
