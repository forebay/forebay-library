#include "accounts_manage.h"

#include <stdio.h>

#include "accounts_secrets.h"
#include "accounts_support.h"

#define TOKEN_FIX "keep tokens in the plugin's secrets (ctx.secrets) and store only non-secret account state"

typedef struct upsert_request {
    const char *store;
    const cJSON *account;
    const char *id;
} upsert_request;

typedef struct remove_request {
    const char *store;
    const char *id;
} remove_request;

static cJSON *text_answer(const char *name, const char *text) {
    cJSON *answer = cJSON_CreateObject();
    if (answer == NULL || cJSON_AddStringToObject(answer, name, text) == NULL) {
        cJSON_Delete(answer);
        return NULL;
    }
    return answer;
}

static cJSON *flag_answer(const char *name, int value) {
    cJSON *answer = cJSON_CreateObject();
    if (answer == NULL || cJSON_AddBoolToObject(answer, name, value) == NULL) {
        cJSON_Delete(answer);
        return NULL;
    }
    return answer;
}

static cJSON *account_entries(const cJSON *accounts) {
    cJSON *entries = cJSON_CreateArray();
    const cJSON *account = NULL;
    cJSON_ArrayForEach(account, accounts) {
        if (cJSON_IsObject(account) && !service_push(entries, cJSON_Duplicate(account, 1))) {
            cJSON_Delete(entries);
            return NULL;
        }
    }
    return entries;
}

/** An unreadable store lists as empty, as AccountStore.parseOrDefault degrades a corrupted one. */
SERVICE_EXPORT int32_t accounts_list(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len) {
    spisor_call call;
    int32_t status = service_open(in, in_len, &call, out, out_len);
    if (status != SPISOR_STATUS_SUCCESS) {
        return status;
    }
    const char *path = NULL;
    if (accounts_require_store(&call, &path, out, out_len, &status)) {
        accounts_store store;
        accounts_store_status opened = accounts_store_open(path, 0, &store);
        if (opened == ACCOUNTS_STORE_OUT_OF_MEMORY) {
            status = service_out_of_memory(out, out_len);
        } else {
            cJSON *entries = opened == ACCOUNTS_STORE_READY ? account_entries(accounts_store_accounts(&store))
                                                            : cJSON_CreateArray();
            status = service_answer(service_wrap("accounts", entries), out, out_len);
            accounts_store_close(&store);
        }
    }
    spisor_discard_call(&call);
    return status;
}

/** The incoming members laid over a copy of the stored account, in the stored order. */
static cJSON *merged_account(const cJSON *stored, const cJSON *incoming) {
    cJSON *merged = cJSON_Duplicate(stored, 1);
    const cJSON *member = NULL;
    cJSON_ArrayForEach(member, incoming) {
        if (merged == NULL || !accounts_set_item(merged, member->string, cJSON_Duplicate(member, 1))) {
            cJSON_Delete(merged);
            return NULL;
        }
    }
    return merged;
}

static void add_account(cJSON *accounts, const upsert_request *request, accounts_outcome *outcome) {
    if (service_push(accounts, cJSON_Duplicate(request->account, 1))) {
        outcome->result = text_answer("outcome", "added");
        outcome->changed = 1;
    }
}

static void update_account(cJSON *accounts, int index, const upsert_request *request, accounts_outcome *outcome) {
    cJSON *stored = cJSON_GetArrayItem(accounts, index);
    cJSON *merged = merged_account(stored, request->account);
    if (merged == NULL) {
        return;
    }
    if (cJSON_Compare(merged, stored, 1)) {
        cJSON_Delete(merged);
        outcome->result = text_answer("outcome", "unchanged");
        return;
    }
    if (!cJSON_ReplaceItemInArray(accounts, index, merged)) {
        cJSON_Delete(merged);
        return;
    }
    outcome->result = text_answer("outcome", "updated");
    outcome->changed = 1;
}

static void upsert_into(accounts_store *store, const void *given, accounts_outcome *outcome) {
    const upsert_request *request = given;
    cJSON *accounts = accounts_store_accounts(store);
    int index = 0;
    if (accounts_find(accounts, request->id, &index) == NULL) {
        add_account(accounts, request, outcome);
    } else {
        update_account(accounts, index, request, outcome);
    }
}

static int read_account(const spisor_call *call, upsert_request *request, uint8_t **out, size_t *out_len,
                        int32_t *status) {
    if (!service_require(call, "account", service_is_object, "an object", &request->account, out, out_len, status)) {
        return 0;
    }
    request->id = accounts_account_id(request->account);
    if (request->id == NULL) {
        *status = service_fail("account.id", "is missing or is not a non-empty string",
                               "send the account with its id", out, out_len);
        return 0;
    }
    return 1;
}

static int32_t refuse_token_member(const char *member, uint8_t **out, size_t *out_len) {
    char cause[200];
    snprintf(cause, sizeof(cause), "the account carries the token member %s", member);
    return service_refuse(cause, TOKEN_FIX, out, out_len);
}

SERVICE_EXPORT int32_t accounts_upsert(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len) {
    spisor_call call;
    int32_t status = service_open(in, in_len, &call, out, out_len);
    if (status != SPISOR_STATUS_SUCCESS) {
        return status;
    }
    upsert_request request;
    if (accounts_require_store(&call, &request.store, out, out_len, &status) &&
        read_account(&call, &request, out, out_len, &status)) {
        const char *token_member = accounts_find_token_member(request.account);
        status = token_member != NULL ? refuse_token_member(token_member, out, out_len)
                                      : accounts_run_change(request.store, upsert_into, &request, out, out_len);
    }
    spisor_discard_call(&call);
    return status;
}

static void remove_from(accounts_store *store, const void *given, accounts_outcome *outcome) {
    const remove_request *request = given;
    cJSON *accounts = accounts_store_accounts(store);
    int index = 0;
    int found = accounts_find(accounts, request->id, &index) != NULL;
    if (found) {
        cJSON_DeleteItemFromArray(accounts, index);
    }
    outcome->result = flag_answer("removed", found);
    outcome->changed = found;
}

SERVICE_EXPORT int32_t accounts_remove(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len) {
    spisor_call call;
    int32_t status = service_open(in, in_len, &call, out, out_len);
    if (status != SPISOR_STATUS_SUCCESS) {
        return status;
    }
    remove_request request;
    const cJSON *id = NULL;
    if (accounts_require_store(&call, &request.store, out, out_len, &status) &&
        service_require(&call, "id", service_is_string, "a string", &id, out, out_len, &status)) {
        request.id = id->valuestring;
        status = accounts_run_change(request.store, remove_from, &request, out, out_len);
    }
    spisor_discard_call(&call);
    return status;
}
