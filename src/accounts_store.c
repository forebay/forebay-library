#include "accounts_store.h"

#include <string.h>

#include "spisor/allocation.h"
#include "spisor/file.h"
#include "spisor/json_file.h"
#include "spisor/json_write.h"

#define LOCK_SUFFIX ".lock"

static char *with_suffix(const char *path, const char *suffix) {
    size_t path_length = strlen(path);
    char *joined = spisor_memory_allocate(path_length + strlen(suffix) + 1);
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
    char *folder = spisor_memory_allocate(length + 1);
    if (folder != NULL) {
        memcpy(folder, path, length);
        folder[length] = '\0';
    }
    return folder;
}

/** READY when the folder holding path exists or was made, OUT_OF_MEMORY, or `refused` when it cannot be made. */
static accounts_store_status ensure_folder_of(const char *path, accounts_store_status refused) {
    char *folder = folder_of(path);
    spisor_file_presence presence = folder == NULL ? SPISOR_FILE_PRESENT : spisor_probe_file(folder);
    spisor_status made = SPISOR_OK;
    if (presence == SPISOR_FILE_MISSING || presence == SPISOR_FILE_UNKNOWN) {
        made = spisor_make_directories(folder);
    }
    spisor_memory_release(folder);
    if (presence == SPISOR_FILE_OUT_OF_MEMORY || made == SPISOR_OUT_OF_MEMORY) {
        return ACCOUNTS_STORE_OUT_OF_MEMORY;
    }
    return made == SPISOR_OK ? ACCOUNTS_STORE_READY : refused;
}

static accounts_store_status lock_beside(const char *path, spisor_lock **guard) {
    char *lock_path = with_suffix(path, LOCK_SUFFIX);
    spisor_status status = lock_path == NULL ? SPISOR_OUT_OF_MEMORY : spisor_lock_file(lock_path, guard);
    spisor_memory_release(lock_path);
    if (status == SPISOR_OUT_OF_MEMORY) {
        return ACCOUNTS_STORE_OUT_OF_MEMORY;
    }
    return status == SPISOR_OK ? ACCOUNTS_STORE_READY : ACCOUNTS_STORE_UNLOCKABLE;
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
    spisor_json_file_state state = SPISOR_JSON_FILE_UNREADABLE;
    cJSON *parsed = spisor_read_json_file(path, &state);
    if (state == SPISOR_JSON_FILE_OUT_OF_MEMORY) {
        return ACCOUNTS_STORE_OUT_OF_MEMORY;
    }
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
    accounts_store_status status = ensure_folder_of(store->path, ACCOUNTS_STORE_UNLOCKABLE);
    return status == ACCOUNTS_STORE_READY ? lock_beside(store->path, &store->guard) : status;
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
    accounts_store_status status = ensure_folder_of(store->path, ACCOUNTS_STORE_UNWRITABLE);
    if (status == ACCOUNTS_STORE_READY) {
        spisor_status written = spisor_replace_file(store->path, (const uint8_t *)text, strlen(text));
        status = written == SPISOR_OK ? ACCOUNTS_STORE_READY
                 : written == SPISOR_OUT_OF_MEMORY ? ACCOUNTS_STORE_OUT_OF_MEMORY : ACCOUNTS_STORE_UNWRITABLE;
    }
    spisor_memory_release(text);
    return status;
}

void accounts_store_close(accounts_store *store) {
    spisor_unlock(store->guard);
    cJSON_Delete(store->document);
    spisor_memory_release(store->path);
    memset(store, 0, sizeof(*store));
}

cJSON *accounts_store_accounts(const accounts_store *store) {
    return cJSON_GetObjectItemCaseSensitive(store->document, "accounts");
}
