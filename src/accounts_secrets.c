#include "accounts_secrets.h"

#include <string.h>

#include "service_support.h"

static int names_a_token(const char *name) {
    size_t length = strlen(name);
    return service_contains_ignoring_case(name, "token") ||
           (length == strlen("access") && service_contains_ignoring_case(name, "access")) ||
           (length == strlen("refresh") && service_contains_ignoring_case(name, "refresh"));
}

const char *accounts_find_token_member(const cJSON *value) {
    const cJSON *child = NULL;
    cJSON_ArrayForEach(child, value) {
        if (cJSON_IsObject(value) && child->string != NULL && names_a_token(child->string)) {
            return child->string;
        }
        const char *nested = accounts_find_token_member(child);
        if (nested != NULL) {
            return nested;
        }
    }
    return NULL;
}
