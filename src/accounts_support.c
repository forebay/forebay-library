#include "accounts_support.h"

#include <string.h>

#include "definition_support.h"

spisor_status accounts_refuse_store(accounts_store_status status, spisor_refusal *refusal) {
    switch (status) {
    case ACCOUNTS_STORE_READY:
        return SPISOR_OK;
    case ACCOUNTS_STORE_UNLOCKABLE:
        return forebay_refuse_as(refusal, SPISOR_REFUSED, "the account store could not be locked",
                                 "check that the store's folder and its .lock file can be created, opened and locked");
    case ACCOUNTS_STORE_UNREADABLE:
        return forebay_refuse_as(refusal, SPISOR_REFUSED, "the account store is not valid JSON",
                                 "repair or delete the account store file, then retry");
    case ACCOUNTS_STORE_UNWRITABLE:
        return forebay_refuse_as(refusal, SPISOR_REFUSED, "the account store could not be written",
                                 "check that the store's folder is writable and has room");
    default:
        return SPISOR_OUT_OF_MEMORY;
    }
}

static void release_filled(accounts_answer_release release, void *answer) {
    if (release != NULL) {
        release(answer);
    }
}

spisor_status accounts_run_change(const char *path, accounts_change change, const void *request, void *answer,
                                  accounts_answer_release release, spisor_refusal *refusal) {
    accounts_store store;
    int changed = 0;
    accounts_store_status opened = accounts_store_open(path, 1, &store);
    if (opened != ACCOUNTS_STORE_READY) {
        return accounts_refuse_store(opened, refusal);
    }
    spisor_status status = change(&store, request, answer, &changed);
    accounts_store_status saved = ACCOUNTS_STORE_READY;
    if (status == SPISOR_OK && changed) {
        saved = accounts_store_save(&store);
    }
    accounts_store_close(&store);
    if (status == SPISOR_OK && saved != ACCOUNTS_STORE_READY) {
        release_filled(release, answer);
        status = accounts_refuse_store(saved, refusal);
    }
    return status;
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
    if (!cJSON_AddItemToObject(object, name, item)) {
        cJSON_Delete(item);
        return 0;
    }
    return 1;
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
