#include "service_support.h"

#include <stdio.h>
#include <string.h>

int32_t service_out_of_memory(uint8_t **out, size_t *out_len) {
    *out = NULL;
    *out_len = 0;
    return SPISOR_STATUS_OUT_OF_MEMORY;
}

int32_t service_answer(cJSON *result, uint8_t **out, size_t *out_len) {
    if (result == NULL) {
        return service_out_of_memory(out, out_len);
    }
    int32_t status = spisor_succeed(result, out, out_len);
    cJSON_Delete(result);
    return status;
}

int32_t service_refuse(const char *cause, const char *fix, uint8_t **out, size_t *out_len) {
    cJSON *refusal = cJSON_CreateObject();
    if (refusal == NULL || cJSON_AddStringToObject(refusal, "refused", cause) == NULL ||
        cJSON_AddStringToObject(refusal, "fix", fix) == NULL) {
        cJSON_Delete(refusal);
        return service_out_of_memory(out, out_len);
    }
    return service_answer(refusal, out, out_len);
}

int32_t service_fail(const char *subject, const char *problem, const char *fix, uint8_t **out, size_t *out_len) {
    char message[256];
    snprintf(message, sizeof(message), "%s %s", subject, problem);
    return spisor_fail(message, fix, out, out_len);
}

int32_t service_open(const uint8_t *in, size_t in_len, spisor_call *call, uint8_t **out, size_t *out_len) {
    memset(call, 0, sizeof(*call));
    int32_t status = spisor_parse_call(in, in_len, 0, call, out, out_len);
    if (status == SPISOR_STATUS_SUCCESS && !cJSON_IsObject(call->argument)) {
        spisor_discard_call(call);
        return spisor_fail("the argument is not an object", "send the argument as a JSON object", out, out_len);
    }
    return status;
}

int service_is_object(const cJSON *value) {
    return cJSON_IsObject(value);
}

int service_is_array(const cJSON *value) {
    return cJSON_IsArray(value);
}

int service_is_string(const cJSON *value) {
    return cJSON_IsString(value);
}

int service_is_number(const cJSON *value) {
    return cJSON_IsNumber(value);
}

int service_is_boolean(const cJSON *value) {
    return cJSON_IsBool(value);
}

int service_require(const spisor_call *call, const char *name, int (*is_type)(const cJSON *), const char *type_name,
                    const cJSON **member, uint8_t **out, size_t *out_len, int32_t *status) {
    *member = cJSON_GetObjectItemCaseSensitive(call->argument, name);
    if (*member != NULL && is_type(*member)) {
        return 1;
    }
    char problem[96];
    char fix[128];
    snprintf(problem, sizeof(problem), "is missing or is not %s", type_name);
    snprintf(fix, sizeof(fix), "send %s as %s", name, type_name);
    *status = service_fail(name, problem, fix, out, out_len);
    return 0;
}

int service_add(cJSON *object, const char *name, cJSON *item) {
    if (item == NULL) {
        return 0;
    }
    if (!cJSON_AddItemToObject(object, name, item)) {
        cJSON_Delete(item);
        return 0;
    }
    return 1;
}

cJSON *service_wrap(const char *name, cJSON *item) {
    cJSON *wrapper = cJSON_CreateObject();
    if (wrapper == NULL) {
        cJSON_Delete(item);
        return NULL;
    }
    if (!service_add(wrapper, name, item)) {
        cJSON_Delete(wrapper);
        return NULL;
    }
    return wrapper;
}

int service_push(cJSON *array, cJSON *item) {
    if (item == NULL) {
        return 0;
    }
    if (!cJSON_AddItemToArray(array, item)) {
        cJSON_Delete(item);
        return 0;
    }
    return 1;
}

static char lower_ascii(char value) {
    return (value >= 'A' && value <= 'Z') ? (char)(value + 32) : value;
}

int service_contains_ignoring_case(const char *haystack, const char *needle) {
    size_t needle_length = strlen(needle);
    size_t haystack_length = strlen(haystack);
    if (needle_length > haystack_length) {
        return 0;
    }
    for (size_t start = 0; start + needle_length <= haystack_length; start++) {
        size_t offset = 0;
        while (offset < needle_length && lower_ascii(haystack[start + offset]) == lower_ascii(needle[offset])) {
            offset++;
        }
        if (offset == needle_length) {
            return 1;
        }
    }
    return 0;
}
