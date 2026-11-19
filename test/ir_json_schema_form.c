#include <string.h>

#include "ir_json.h"

static const char *const OPAQUE_KEYS[] = {"extensions", "raw", "input", "inputSchema", "metadata"};

static int is_opaque_key(const char *key) {
    for (size_t index = 0; index < sizeof OPAQUE_KEYS / sizeof OPAQUE_KEYS[0]; index++) {
        if (strcmp(OPAQUE_KEYS[index], key) == 0) {
            return 1;
        }
    }
    return 0;
}

static int is_kind(const cJSON *object, const char *kind) {
    const char *value = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(object, "kind"));
    return value != NULL && strcmp(value, kind) == 0;
}

/** Whether a null member is one the schema requires, which the form fills rather than drops. */
static int is_required_by_the_schema(const cJSON *object, const char *key) {
    return (strcmp(key, "text") == 0 && is_kind(object, "thinking")) ||
           (strcmp(key, "mediaType") == 0 && is_kind(object, "image")) ||
           (strcmp(key, "content") == 0 && is_kind(object, "tool_result"));
}

static cJSON *filling_of(const char *key) {
    return strcmp(key, "content") == 0 ? cJSON_CreateArray() : cJSON_CreateString("");
}

/** The member's form; NULL with `*dropped` 1 for a null member the form leaves out, else NULL when memory runs out. */
static cJSON *member_form(const cJSON *object, const cJSON *member, int *dropped) {
    *dropped = 0;
    if (is_opaque_key(member->string)) {
        return cJSON_Duplicate(member, 1);
    }
    if (!cJSON_IsNull(member)) {
        return ir_json_schema_form(member);
    }
    if (is_required_by_the_schema(object, member->string)) {
        return filling_of(member->string);
    }
    *dropped = 1;
    return NULL;
}

static cJSON *object_form(const cJSON *object) {
    cJSON *form = cJSON_CreateObject();
    const cJSON *member = NULL;
    cJSON_ArrayForEach(member, object) {
        int dropped = 0;
        cJSON *value = form == NULL ? NULL : member_form(object, member, &dropped);
        if (dropped) {
            continue;
        }
        if (value == NULL || !cJSON_AddItemToObject(form, member->string, value)) {
            cJSON_Delete(value);
            cJSON_Delete(form);
            return NULL;
        }
    }
    return form;
}

static cJSON *array_form(const cJSON *array) {
    cJSON *form = cJSON_CreateArray();
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, array) {
        cJSON *value = form == NULL ? NULL : ir_json_schema_form(item);
        if (value == NULL || !cJSON_AddItemToArray(form, value)) {
            cJSON_Delete(value);
            cJSON_Delete(form);
            return NULL;
        }
    }
    return form;
}

cJSON *ir_json_schema_form(const cJSON *node) {
    if (cJSON_IsObject(node)) {
        return object_form(node);
    }
    if (cJSON_IsArray(node)) {
        return array_form(node);
    }
    return cJSON_Duplicate(node, 1);
}
