#include "service_support.h"

#include <stdio.h>
#include <string.h>

int32_t service_out_of_memory(service_reply *reply) {
    reply->status = SERVICE_OUT_OF_MEMORY;
    return reply->status;
}

int32_t service_answer(cJSON *result, service_reply *reply) {
    if (result == NULL) {
        return service_out_of_memory(reply);
    }
    reply->result = result;
    reply->status = SERVICE_SUCCESS;
    return reply->status;
}

int32_t service_refuse(const char *cause, const char *fix, service_reply *reply) {
    cJSON *refusal = cJSON_CreateObject();
    if (refusal == NULL || cJSON_AddStringToObject(refusal, "refused", cause) == NULL ||
        cJSON_AddStringToObject(refusal, "fix", fix) == NULL) {
        cJSON_Delete(refusal);
        return service_out_of_memory(reply);
    }
    return service_answer(refusal, reply);
}

int32_t service_fail(const char *subject, const char *problem, const char *fix, service_reply *reply) {
    snprintf(reply->message, sizeof reply->message, "%s %s", subject, problem);
    snprintf(reply->fix, sizeof reply->fix, "%s", fix);
    reply->status = SERVICE_MALFORMED;
    return reply->status;
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
                    const cJSON **member, service_reply *reply, int32_t *status) {
    *member = cJSON_GetObjectItemCaseSensitive(call->argument, name);
    if (*member != NULL && is_type(*member)) {
        return 1;
    }
    char problem[96];
    char fix[128];
    snprintf(problem, sizeof(problem), "is missing or is not %s", type_name);
    snprintf(fix, sizeof(fix), "set %s to %s", name, type_name);
    *status = service_fail(name, problem, fix, reply);
    return 0;
}

int service_require_member(const cJSON *object, const char *subject, const char *name, int (*is_type)(const cJSON *),
                           const char *type_name, const cJSON **member, service_reply *reply,
                           int32_t *status) {
    *member = cJSON_GetObjectItemCaseSensitive(object, name);
    if (*member != NULL && is_type(*member)) {
        return 1;
    }
    char problem[128];
    char fix[128];
    snprintf(problem, sizeof(problem), "%s is missing or is not %s", name, type_name);
    snprintf(fix, sizeof(fix), "set %s to %s", name, type_name);
    *status = service_fail(subject, problem, fix, reply);
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
