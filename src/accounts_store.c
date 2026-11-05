#include "accounts_store.h"

#include <stdlib.h>
#include <string.h>

#include "spisor/file.h"
#include "spisor/json_file.h"
#include "spisor/json_write.h"

#define LOCK_SUFFIX ".lock"

static char *with_suffix(const char *path, const char *suffix) {
    size_t path_length = strlen(path);
    char *joined = malloc(path_length + strlen(suffix) + 1);
    if (joined != NULL) {
        memcpy(joined, path, path_length);
        strcpy(joined + path_length, suffix);
    }
    return joined;
}

/** The folder holding path, or NULL when path names a file in the current folder or the root. */
static char *folder_of(const char *path) {
    const char *separator = NULL;
    for (const char *cursor = path; *cursor != '\0'; cursor++) {
        if (*cursor == '/' || *cursor == '\\') {
            separator = cursor;
        }
    }
    if (separator == NULL || separator == path) {
        return NULL;
    }
    size_t length = (size_t)(separator - path);
    char *folder = malloc(length + 1);
    if (folder != NULL) {
        memcpy(folder, path, length);
        folder[length] = '\0';
    }
    return folder;
}

static int ensure_folder_of(const char *path) {
    char *folder = folder_of(path);
    int ready = folder == NULL || spisor_file_exists(folder) || spisor_make_directories(folder);
    free(folder);
    return ready;
}

static runtime_hosts_guard *lock_beside(const char *path) {
    char *lock_path = with_suffix(path, LOCK_SUFFIX);
    runtime_hosts_guard *guard = lock_path == NULL ? NULL : runtime_lock_file(lock_path);
    free(lock_path);
    return guard;
}

static int ensure_array(cJSON *document, const char *name) {
    if (cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(document, name))) {
        return 1;
    }
    cJSON_DeleteItemFromObjectCaseSensitive(document, name);
    return cJSON_AddArrayToObject(document, name) != NULL;
}

static int ensure_number(cJSON *document, const char *name, double fallback) {
    if (cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(document, name))) {
        return 1;
    }
    cJSON_DeleteItemFromObjectCaseSensitive(document, name);
    return cJSON_AddNumberToObject(document, name, fallback) != NULL;
}

static int ensure_lane_cursors(cJSON *document) {
    cJSON *cursors = cJSON_GetObjectItemCaseSensitive(document, "activeIndexByLane");
    if (!cJSON_IsObject(cursors)) {
        cJSON_DeleteItemFromObjectCaseSensitive(document, "activeIndexByLane");
        return cJSON_AddObjectToObject(document, "activeIndexByLane") != NULL;
    }
    cJSON *cursor = cursors->child;
    while (cursor != NULL) {
        cJSON *next = cursor->next;
        if (!cJSON_IsNumber(cursor)) {
            cJSON_DeleteItemFromObjectCaseSensitive(cursors, cursor->string);
        }
        cursor = next;
    }
    return 1;
}

static int ensure_version(cJSON *document) {
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(document, "version");
    if (cJSON_IsNumber(version) && version->valuedouble == 1) {
        return 1;
    }
    cJSON_DeleteItemFromObjectCaseSensitive(document, "version");
    return cJSON_AddNumberToObject(document, "version", 1) != NULL;
}

/** Gives the document its four pool members, leaving every other member as it was. */
static int normalize(cJSON *document) {
    return ensure_version(document) && ensure_array(document, "accounts") &&
           ensure_number(document, "activeIndex", 0) && ensure_lane_cursors(document);
}

static accounts_store_status read_document(const char *path, cJSON **document) {
    spisor_json_file_state state = SPISOR_JSON_FILE_ABSENT;
    cJSON *parsed = spisor_read_json_file(path, &state);
    if (state == SPISOR_JSON_FILE_ABSENT) {
        *document = cJSON_CreateObject();
        return *document == NULL ? ACCOUNTS_STORE_OUT_OF_MEMORY : ACCOUNTS_STORE_READY;
    }
    if (state != SPISOR_JSON_FILE_PARSED || !cJSON_IsObject(parsed)) {
        cJSON_Delete(parsed);
        return ACCOUNTS_STORE_UNREADABLE;
    }
    *document = parsed;
    return ACCOUNTS_STORE_READY;
}

static accounts_store_status load(accounts_store *store) {
    accounts_store_status status = read_document(store->path, &store->document);
    if (status == ACCOUNTS_STORE_READY && !normalize(store->document)) {
        status = ACCOUNTS_STORE_OUT_OF_MEMORY;
    }
    return status;
}

static accounts_store_status take_lock(accounts_store *store) {
    if (!ensure_folder_of(store->path)) {
        return ACCOUNTS_STORE_UNLOCKABLE;
    }
    store->guard = lock_beside(store->path);
    return store->guard == NULL ? ACCOUNTS_STORE_UNLOCKABLE : ACCOUNTS_STORE_READY;
}

accounts_store_status accounts_store_open(const char *path, int locked, accounts_store *store) {
    memset(store, 0, sizeof(*store));
    store->path = with_suffix(path, "");
    accounts_store_status status = store->path == NULL ? ACCOUNTS_STORE_OUT_OF_MEMORY : ACCOUNTS_STORE_READY;
    if (status == ACCOUNTS_STORE_READY && locked) {
        status = take_lock(store);
    }
    if (status == ACCOUNTS_STORE_READY) {
        status = load(store);
    }
    if (status != ACCOUNTS_STORE_READY) {
        accounts_store_close(store);
    }
    return status;
}

accounts_store_status accounts_store_save(const accounts_store *store) {
    char *text = spisor_json_stringify_pretty(store->document);
    if (text == NULL) {
        return ACCOUNTS_STORE_OUT_OF_MEMORY;
    }
    int written = ensure_folder_of(store->path) &&
                  spisor_replace_file(store->path, (const uint8_t *)text, strlen(text));
    free(text);
    return written ? ACCOUNTS_STORE_READY : ACCOUNTS_STORE_UNWRITABLE;
}

void accounts_store_close(accounts_store *store) {
    runtime_hosts_unlock(store->guard);
    cJSON_Delete(store->document);
    free(store->path);
    memset(store, 0, sizeof(*store));
}

cJSON *accounts_store_accounts(const accounts_store *store) {
    return cJSON_GetObjectItemCaseSensitive(store->document, "accounts");
}
