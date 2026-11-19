#include "anthropic_writer.h"

#include <string.h>

#include "spisor/json_value.h"

static int is_failed(const anthropic_writer *writer) {
    return writer->status != SPISOR_OK;
}

static void fail(anthropic_writer *writer, spisor_status status, const char *member) {
    if (!is_failed(writer)) {
        writer->status = status;
        writer->member = member;
    }
}

cJSON *anthropic_new_object(anthropic_writer *writer) {
    cJSON *object = is_failed(writer) ? NULL : cJSON_CreateObject();
    if (object == NULL) {
        fail(writer, SPISOR_OUT_OF_MEMORY, NULL);
    }
    return object;
}

cJSON *anthropic_new_array(anthropic_writer *writer) {
    cJSON *array = is_failed(writer) ? NULL : cJSON_CreateArray();
    if (array == NULL) {
        fail(writer, SPISOR_OUT_OF_MEMORY, NULL);
    }
    return array;
}

void anthropic_append(anthropic_writer *writer, cJSON *array, cJSON *item) {
    if (item == NULL) {
        fail(writer, SPISOR_OUT_OF_MEMORY, NULL);
    } else if (is_failed(writer) || array == NULL || !cJSON_AddItemToArray(array, item)) {
        cJSON_Delete(item);
        fail(writer, SPISOR_OUT_OF_MEMORY, NULL);
    }
}

void anthropic_set(anthropic_writer *writer, cJSON *object, const char *key, cJSON *item) {
    if (item == NULL) {
        fail(writer, SPISOR_OUT_OF_MEMORY, NULL);
        return;
    }
    int attached = 0;
    if (!is_failed(writer) && object != NULL) {
        attached = cJSON_GetObjectItemCaseSensitive(object, key) != NULL
                       ? cJSON_ReplaceItemInObjectCaseSensitive(object, key, item)
                       : cJSON_AddItemToObject(object, key, item);
    }
    if (!attached) {
        cJSON_Delete(item);
        fail(writer, SPISOR_OUT_OF_MEMORY, NULL);
    }
}

void anthropic_set_text(anthropic_writer *writer, cJSON *object, const char *key, const char *text) {
    anthropic_set(writer, object, key, is_failed(writer) ? NULL : cJSON_CreateString(text));
}

void anthropic_set_count(anthropic_writer *writer, cJSON *object, const char *key, uint64_t count) {
    anthropic_set_number(writer, object, key, (double)count);
}

void anthropic_set_number(anthropic_writer *writer, cJSON *object, const char *key, double number) {
    anthropic_set(writer, object, key, is_failed(writer) ? NULL : cJSON_CreateNumber(number));
}

void anthropic_set_flag(anthropic_writer *writer, cJSON *object, const char *key, uint32_t flag) {
    anthropic_set(writer, object, key, is_failed(writer) ? NULL : cJSON_CreateBool(flag != 0));
}

cJSON *anthropic_json_of(anthropic_writer *writer, const spisor_value *value, const char *member) {
    cJSON *json = NULL;
    if (is_failed(writer)) {
        return NULL;
    }
    spisor_status status = spisor_json_of_value(value, &json);
    if (status != SPISOR_OK) {
        fail(writer, status, member);
    }
    return json;
}

void anthropic_set_value(anthropic_writer *writer, cJSON *object, const char *key, const spisor_value *value,
                         const char *member) {
    cJSON *json = anthropic_json_of(writer, value, member);
    if (json != NULL) {
        anthropic_set(writer, object, key, json);
    }
}

void anthropic_set_record(anthropic_writer *writer, cJSON *object, const spisor_value *record, const char *member) {
    if (anthropic_value_of_kind(record, SPISOR_VALUE_RECORD) == NULL) {
        return;
    }
    for (size_t index = 0; index < record->count; index++) {
        anthropic_set_value(writer, object, record->entries[index].key, &record->entries[index].value, member);
    }
}

const spisor_value *anthropic_value_of_kind(const spisor_value *value, spisor_value_kind kind) {
    return value != NULL && value->kind == (uint32_t)kind ? value : NULL;
}

const spisor_value *anthropic_extension_of(const spisor_value *extensions, const char *key) {
    return anthropic_value_of_kind(extensions, SPISOR_VALUE_RECORD) == NULL
               ? NULL
               : spisor_value_member(extensions, key, strlen(key));
}

int anthropic_extension_flag_is_set(const spisor_value *extensions, const char *key) {
    const spisor_value *flag = anthropic_value_of_kind(anthropic_extension_of(extensions, key), SPISOR_VALUE_BOOLEAN);
    return flag != NULL && flag->boolean == 1;
}
