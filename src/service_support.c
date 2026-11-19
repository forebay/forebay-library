#include "service_support.h"

#include <stdio.h>
#include <string.h>

int32_t service_out_of_memory(uint8_t **out, size_t *out_len) {
    *out = NULL;
    *out_len = 0;
    return SPISOR_STATUS_OUT_OF_MEMORY;
}

static int32_t emit_printed(char *printed, int32_t status, uint8_t **out, size_t *out_len) {
    if (printed == NULL) {
        return service_out_of_memory(out, out_len);
    }
    *out = (uint8_t *)printed;
    *out_len = strlen(printed);
    return status;
}

int32_t service_answer(cJSON *result, uint8_t **out, size_t *out_len) {
    if (result == NULL) {
        return service_out_of_memory(out, out_len);
    }
    int32_t status = emit_printed(cJSON_PrintUnformatted(result), SPISOR_STATUS_SUCCESS, out, out_len);
    cJSON_Delete(result);
    return status;
}

static int32_t fail_with(const char *message, const char *fix, uint8_t **out, size_t *out_len) {
    cJSON *failure = cJSON_CreateObject();
    if (failure == NULL || cJSON_AddStringToObject(failure, "message", message) == NULL ||
        cJSON_AddStringToObject(failure, "fix", fix) == NULL) {
        cJSON_Delete(failure);
        return service_out_of_memory(out, out_len);
    }
    char *printed = cJSON_PrintUnformatted(failure);
    cJSON_Delete(failure);
    return emit_printed(printed, SPISOR_STATUS_FAILURE, out, out_len);
}

void service_discard_call(service_call *call) {
    cJSON_Delete(call->argument);
    call->argument = NULL;
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
    return fail_with(message, fix, out, out_len);
}

int32_t service_open(const uint8_t *in, size_t in_len, service_call *call, uint8_t **out, size_t *out_len) {
    int out_of_memory = 0;
    *out = NULL;
    *out_len = 0;
    call->argument = spisor_parse_json_reporting(in, in_len, &out_of_memory);
    if (out_of_memory) {
        return service_out_of_memory(out, out_len);
    }
    if (call->argument == NULL) {
        return fail_with("the input is not valid JSON", "send exactly one JSON value", out, out_len);
    }
    if (!cJSON_IsObject(call->argument)) {
        service_discard_call(call);
        return fail_with("the argument is not an object", "send the argument as a JSON object", out, out_len);
    }
    return SPISOR_STATUS_SUCCESS;
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

int service_require(const service_call *call, const char *name, int (*is_type)(const cJSON *), const char *type_name,
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

int service_require_member(const cJSON *object, const char *subject, const char *name, int (*is_type)(const cJSON *),
                           const char *type_name, const cJSON **member, uint8_t **out, size_t *out_len,
                           int32_t *status) {
    *member = cJSON_GetObjectItemCaseSensitive(object, name);
    if (*member != NULL && is_type(*member)) {
        return 1;
    }
    char problem[128];
    char fix[128];
    snprintf(problem, sizeof(problem), "%s is missing or is not %s", name, type_name);
    snprintf(fix, sizeof(fix), "send %s as %s", name, type_name);
    *status = service_fail(subject, problem, fix, out, out_len);
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
