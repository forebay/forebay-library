#include "anthropic_reader.h"

#include <math.h>
#include <string.h>

#include "answer_storage.h"

static const spisor_value null_value = {SPISOR_VALUE_NULL, 0, 0, NULL, 0, NULL, NULL, 0};

const spisor_value *anthropic_null_value(void) {
    return &null_value;
}

void *anthropic_take(anthropic_reader *reader, size_t count, size_t size) {
    if (reader->out_of_memory) {
        return NULL;
    }
    void *taken = forebay_storage_array(reader->storage, count, size);
    if (taken == NULL && count > 0) {
        reader->out_of_memory = 1;
    }
    return taken;
}

const char *anthropic_copy_text(anthropic_reader *reader, const char *text) {
    if (text == NULL || reader->out_of_memory) {
        return NULL;
    }
    const char *copy = NULL;
    if (!forebay_storage_text(reader->storage, text, &copy)) {
        reader->out_of_memory = 1;
    }
    return copy;
}

static void fill_list(anthropic_reader *reader, const cJSON *json, spisor_value *target) {
    size_t count = (size_t)cJSON_GetArraySize(json);
    spisor_value *items = anthropic_take(reader, count, sizeof *items);
    size_t index = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, json) {
        if (items == NULL) {
            return;
        }
        anthropic_fill_value(reader, item, &items[index++]);
    }
    target->kind = SPISOR_VALUE_LIST;
    target->items = items;
    target->count = count;
}

static int is_skipped(const cJSON *member, const char *skipped_key) {
    return skipped_key != NULL && strcmp(member->string, skipped_key) == 0;
}

static size_t members_except(const cJSON *record, const char *skipped_key) {
    size_t count = 0;
    const cJSON *member = NULL;
    cJSON_ArrayForEach(member, record) {
        count += is_skipped(member, skipped_key) ? 0u : 1u;
    }
    return count;
}

static void fill_record(anthropic_reader *reader, const cJSON *json, const char *skipped_key, spisor_value *target) {
    size_t count = members_except(json, skipped_key);
    spisor_entry *entries = anthropic_take(reader, count, sizeof *entries);
    size_t index = 0;
    const cJSON *member = NULL;
    cJSON_ArrayForEach(member, json) {
        if (is_skipped(member, skipped_key)) {
            continue;
        }
        if (entries == NULL) {
            return;
        }
        entries[index].key_length = strlen(member->string);
        entries[index].key = anthropic_copy_text(reader, member->string);
        anthropic_fill_value(reader, member, &entries[index].value);
        index++;
    }
    target->kind = SPISOR_VALUE_RECORD;
    target->entries = entries;
    target->count = count;
}

void anthropic_fill_value(anthropic_reader *reader, const cJSON *json, spisor_value *target) {
    if (reader->out_of_memory) {
        return;
    }
    if (cJSON_IsBool(json)) {
        target->kind = SPISOR_VALUE_BOOLEAN;
        target->boolean = cJSON_IsTrue(json) ? 1 : 0;
    } else if (cJSON_IsNumber(json) && isfinite(json->valuedouble)) {
        target->kind = SPISOR_VALUE_NUMBER;
        target->number = json->valuedouble;
    } else if (cJSON_IsString(json)) {
        target->kind = SPISOR_VALUE_TEXT;
        target->length = strlen(json->valuestring);
        target->text = anthropic_copy_text(reader, json->valuestring);
    } else if (cJSON_IsArray(json)) {
        fill_list(reader, json, target);
    } else if (cJSON_IsObject(json)) {
        fill_record(reader, json, NULL, target);
    } else {
        target->kind = SPISOR_VALUE_NULL;
    }
}

const spisor_value *anthropic_copy_value(anthropic_reader *reader, const cJSON *json) {
    spisor_value *value = anthropic_take(reader, 1, sizeof *value);
    if (value != NULL) {
        anthropic_fill_value(reader, json, value);
    }
    return value;
}

const spisor_value *anthropic_copy_record_without(anthropic_reader *reader, const cJSON *record,
                                                  const char *skipped_key) {
    spisor_value *value = anthropic_take(reader, 1, sizeof *value);
    if (value != NULL) {
        fill_record(reader, record, skipped_key, value);
    }
    return value;
}

void anthropic_extensions_begin(anthropic_reader *reader, anthropic_extension_list *list, size_t translator_keys,
                                size_t wire_members) {
    list->capacity = translator_keys + wire_members;
    list->entries = anthropic_take(reader, list->capacity, sizeof *list->entries);
    list->count = 0;
    list->translator_count = 0;
}

static spisor_entry *entry_named(const anthropic_extension_list *list, size_t searched, const char *key) {
    for (size_t index = 0; index < searched; index++) {
        if (strcmp(list->entries[index].key, key) == 0) {
            return &list->entries[index];
        }
    }
    return NULL;
}

static spisor_entry *entry_for(anthropic_reader *reader, anthropic_extension_list *list, size_t searched,
                               const char *key) {
    if (list->entries == NULL || reader->out_of_memory) {
        return NULL;
    }
    spisor_entry *existing = entry_named(list, searched, key);
    if (existing != NULL || list->count == list->capacity) {
        return existing;
    }
    spisor_entry *added = &list->entries[list->count++];
    added->key = anthropic_copy_text(reader, key);
    added->key_length = strlen(key);
    return added;
}

void anthropic_extensions_put_value(anthropic_reader *reader, anthropic_extension_list *list, const char *key,
                                    const spisor_value *value) {
    spisor_entry *entry = entry_for(reader, list, list->count, key);
    if (entry != NULL && value != NULL) {
        entry->value = *value;
        list->translator_count = list->count;
    }
}

void anthropic_extensions_put_flag(anthropic_reader *reader, anthropic_extension_list *list, const char *key) {
    spisor_value flag = {SPISOR_VALUE_BOOLEAN, 1, 0, NULL, 0, NULL, NULL, 0};
    anthropic_extensions_put_value(reader, list, key, &flag);
}

void anthropic_extensions_put_json(anthropic_reader *reader, anthropic_extension_list *list, const char *key,
                                   const cJSON *json) {
    spisor_value value = {0};
    if (json != NULL) {
        anthropic_fill_value(reader, json, &value);
    } else {
        value = *anthropic_null_value();
    }
    anthropic_extensions_put_value(reader, list, key, &value);
}

static int is_among(const char *key, const char *const *keys, size_t count) {
    for (size_t index = 0; index < count; index++) {
        if (strcmp(key, keys[index]) == 0) {
            return 1;
        }
    }
    return 0;
}

void anthropic_extensions_put_unconsumed(anthropic_reader *reader, anthropic_extension_list *list, const cJSON *wire,
                                         const char *const *consumed, size_t consumed_count) {
    const cJSON *member = NULL;
    cJSON_ArrayForEach(member, wire) {
        if (is_among(member->string, consumed, consumed_count)) {
            continue;
        }
        spisor_entry *entry = entry_for(reader, list, list->translator_count, member->string);
        if (entry != NULL) {
            entry->value = (spisor_value){0};
            anthropic_fill_value(reader, member, &entry->value);
        }
    }
}

const spisor_value *anthropic_extensions_finish(anthropic_reader *reader, const anthropic_extension_list *list) {
    if (list->count == 0) {
        return NULL;
    }
    spisor_value *record = anthropic_take(reader, 1, sizeof *record);
    if (record != NULL) {
        record->kind = SPISOR_VALUE_RECORD;
        record->entries = list->entries;
        record->count = list->count;
    }
    return record;
}
