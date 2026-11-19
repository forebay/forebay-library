#include "anthropic_wire.h"

#include <math.h>

#define LARGEST_EXACT_INTEGER 9007199254740992.0

const cJSON *anthropic_wire_member(const cJSON *object, const char *key) {
    return cJSON_GetObjectItemCaseSensitive(object, key);
}

const char *anthropic_wire_string(const cJSON *item) {
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

const cJSON *anthropic_wire_map(const cJSON *item) {
    return cJSON_IsObject(item) ? item : NULL;
}

const cJSON *anthropic_wire_list(const cJSON *item) {
    return cJSON_IsArray(item) ? item : NULL;
}

int anthropic_wire_boolean(const cJSON *item, uint32_t *value) {
    if (!cJSON_IsBool(item)) {
        return 0;
    }
    *value = cJSON_IsTrue(item) ? 1 : 0;
    return 1;
}

int anthropic_wire_integer(const cJSON *item, uint64_t *value) {
    if (!cJSON_IsNumber(item)) {
        return 0;
    }
    double whole = trunc(item->valuedouble);
    if (!(whole >= 0 && whole <= LARGEST_EXACT_INTEGER)) {
        return 0;
    }
    *value = (uint64_t)whole;
    return 1;
}

int anthropic_wire_number(const cJSON *item, double *value) {
    if (!cJSON_IsNumber(item)) {
        return 0;
    }
    *value = item->valuedouble;
    return 1;
}

size_t anthropic_wire_map_count(const cJSON *list) {
    size_t count = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, list) {
        count += cJSON_IsObject(item) ? 1u : 0u;
    }
    return count;
}

const cJSON *anthropic_wire_next_map(const cJSON *list, const cJSON *previous) {
    const cJSON *item = previous == NULL ? (list == NULL ? NULL : list->child) : previous->next;
    while (item != NULL && !cJSON_IsObject(item)) {
        item = item->next;
    }
    return item;
}

const char *anthropic_wire_string_or_empty(const cJSON *item) {
    const char *text = anthropic_wire_string(item);
    return text == NULL ? "" : text;
}
