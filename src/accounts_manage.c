#include "accounts_manage.h"

#include "accounts_secrets.h"
#include "accounts_support.h"
#include "definition_support.h"
#include "spisor/json_value.h"
#include "spisor/allocation.h"

#define LIST_SHAPE SPISOR_SHAPE_THROUGH(forebay_accounts_list_argument, store)
#define LIST_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_accounts_list_answer, storage)
#define UPSERT_SHAPE SPISOR_SHAPE_THROUGH(forebay_accounts_upsert_argument, account)
#define UPSERT_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_accounts_upsert_answer, outcome)
#define REMOVE_SHAPE SPISOR_SHAPE_THROUGH(forebay_accounts_remove_argument, id)
#define REMOVE_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_accounts_remove_answer, removed)

typedef struct upsert_request {
    const cJSON *account;
    const char *id;
} upsert_request;

static cJSON *account_entries(const cJSON *accounts) {
    cJSON *entries = cJSON_CreateArray();
    const cJSON *account = NULL;
    cJSON_ArrayForEach(account, accounts) {
        cJSON *copy = cJSON_IsObject(account) && entries != NULL ? cJSON_Duplicate(account, 1) : NULL;
        if (cJSON_IsObject(account) && (copy == NULL || !cJSON_AddItemToArray(entries, copy))) {
            cJSON_Delete(copy);
            cJSON_Delete(entries);
            return NULL;
        }
    }
    return entries;
}

static spisor_status fill_list(const cJSON *accounts, forebay_accounts_list_answer *answer) {
    cJSON *entries = account_entries(accounts);
    spisor_value_tree tree = SPISOR_INIT(spisor_value_tree);
    spisor_status status = entries == NULL ? SPISOR_OUT_OF_MEMORY : spisor_value_of_json(entries, &tree);
    cJSON_Delete(entries);
    if (status != SPISOR_OK) {
        return SPISOR_OUT_OF_MEMORY;
    }
    if (tree.root.count == 0) {
        spisor_value_release(&tree);
        return SPISOR_OK;
    }
    answer->accounts = tree.root.items;
    answer->count = tree.root.count;
    answer->storage = tree.storage;
    return SPISOR_OK;
}

/** An unreadable store lists as empty, as AccountStore.parseOrDefault degrades a corrupted one. */
spisor_status accounts_list(void *context, const forebay_accounts_list_argument *argument,
                            forebay_accounts_list_answer *answer, spisor_refusal *refusal) {
    accounts_store store;
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, LIST_SHAPE) ||
        !SPISOR_IS_EMPTY_COUNTED_ANSWER(answer, LIST_ANSWER_SHAPE, answer->count)) {
        return spisor_refuse_shape(refusal, "accounts' list");
    }
    if (!forebay_is_text(argument->store)) {
        return forebay_refuse_member(refusal, "store", "the path of the accounts file");
    }
    accounts_store_status opened = accounts_store_open(argument->store, 0, &store);
    if (opened == ACCOUNTS_STORE_OUT_OF_MEMORY) {
        return SPISOR_OUT_OF_MEMORY;
    }
    if (opened != ACCOUNTS_STORE_READY) {
        return SPISOR_OK;
    }
    spisor_status status = fill_list(accounts_store_accounts(&store), answer);
    accounts_store_close(&store);
    return status;
}

void accounts_release_list_answer(void *context, forebay_accounts_list_answer *answer) {
    (void)context;
    if (answer == NULL || !spisor_has_shape(answer->size, LIST_ANSWER_SHAPE)) {
        return;
    }
    spisor_memory_release(answer->storage);
    SPISOR_CLEAR_ANSWER(answer, LIST_ANSWER_SHAPE);
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

static spisor_status add_account(cJSON *accounts, const upsert_request *request,
                                 forebay_accounts_upsert_answer *answer, int *changed) {
    cJSON *copy = cJSON_Duplicate(request->account, 1);
    if (copy == NULL || !cJSON_AddItemToArray(accounts, copy)) {
        cJSON_Delete(copy);
        return SPISOR_OUT_OF_MEMORY;
    }
    answer->outcome = FOREBAY_ACCOUNTS_ADDED;
    *changed = 1;
    return SPISOR_OK;
}

static spisor_status update_account(cJSON *accounts, int index, const upsert_request *request,
                                    forebay_accounts_upsert_answer *answer, int *changed) {
    cJSON *stored = cJSON_GetArrayItem(accounts, index);
    cJSON *merged = merged_account(stored, request->account);
    if (merged == NULL) {
        return SPISOR_OUT_OF_MEMORY;
    }
    if (cJSON_Compare(merged, stored, 1)) {
        cJSON_Delete(merged);
        answer->outcome = FOREBAY_ACCOUNTS_UNCHANGED;
        return SPISOR_OK;
    }
    if (!cJSON_ReplaceItemInArray(accounts, index, merged)) {
        cJSON_Delete(merged);
        return SPISOR_OUT_OF_MEMORY;
    }
    answer->outcome = FOREBAY_ACCOUNTS_UPDATED;
    *changed = 1;
    return SPISOR_OK;
}

static void clear_upsert_answer(void *filled) {
    forebay_accounts_upsert_answer *answer = filled;
    answer->outcome = 0;
}

static spisor_status upsert_into(accounts_store *store, const void *given, void *filled, int *changed) {
    const upsert_request *request = given;
    cJSON *accounts = accounts_store_accounts(store);
    int index = 0;
    if (accounts_find(accounts, request->id, &index) == NULL) {
        return add_account(accounts, request, filled, changed);
    }
    return update_account(accounts, index, request, filled, changed);
}

int accounts_is_account_record(const spisor_value *account) {
    const spisor_value *id = account == NULL ? NULL : spisor_value_member(account, "id", 2);
    return id != NULL && id->kind == SPISOR_VALUE_TEXT && id->length > 0;
}

static spisor_status upsert_account(const char *path, const cJSON *account, forebay_accounts_upsert_answer *answer,
                                    spisor_refusal *refusal) {
    upsert_request request = {account, accounts_account_id(account)};
    return accounts_run_change(path, upsert_into, &request, answer, clear_upsert_answer, refusal);
}

spisor_status accounts_upsert(void *context, const forebay_accounts_upsert_argument *argument,
                              forebay_accounts_upsert_answer *answer, spisor_refusal *refusal) {
    cJSON *account = NULL;
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, UPSERT_SHAPE) || answer == NULL ||
        !spisor_has_shape(answer->size, UPSERT_ANSWER_SHAPE)) {
        return spisor_refuse_shape(refusal, "accounts' upsert");
    }
    if (!forebay_is_text(argument->store)) {
        return forebay_refuse_member(refusal, "store", "the path of the accounts file");
    }
    if (!accounts_is_account_record(argument->account)) {
        return forebay_refuse_member(refusal, "account", "a record whose id is non-empty text");
    }
    spisor_status status = accounts_refuse_credentials(argument->account, "account", refusal);
    if (status == SPISOR_OK) {
        status = forebay_json_of_member(argument->account, "account", &account, refusal);
    }
    if (status == SPISOR_OK) {
        status = upsert_account(argument->store, account, answer, refusal);
    }
    forebay_delete_overwritten(account);
    return status;
}

static spisor_status remove_from(accounts_store *store, const void *given, void *filled, int *changed) {
    const char *id = given;
    forebay_accounts_remove_answer *answer = filled;
    cJSON *accounts = accounts_store_accounts(store);
    int index = 0;
    int found = accounts_find(accounts, id, &index) != NULL;
    if (found) {
        cJSON_DeleteItemFromArray(accounts, index);
    }
    answer->removed = (uint32_t)found;
    *changed = found;
    return SPISOR_OK;
}

static void clear_remove_answer(void *filled) {
    forebay_accounts_remove_answer *answer = filled;
    answer->removed = 0;
}

spisor_status accounts_remove(void *context, const forebay_accounts_remove_argument *argument,
                              forebay_accounts_remove_answer *answer, spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, REMOVE_SHAPE) || answer == NULL ||
        !spisor_has_shape(answer->size, REMOVE_ANSWER_SHAPE)) {
        return spisor_refuse_shape(refusal, "accounts' remove");
    }
    if (!forebay_is_text(argument->store)) {
        return forebay_refuse_member(refusal, "store", "the path of the accounts file");
    }
    if (argument->id == NULL) {
        return forebay_refuse_member(refusal, "id", "the id of an account");
    }
    return accounts_run_change(argument->store, remove_from, argument->id, answer, clear_remove_answer, refusal);
}
