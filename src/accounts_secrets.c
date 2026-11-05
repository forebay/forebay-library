#include "accounts_secrets.h"

#include <stdio.h>
#include <string.h>

#define TOKEN_FIX "keep tokens in the plugin's secrets (ctx.secrets) and store only non-secret account state"

static const char *const credential_names[] = {"access", "refresh", "accessToken", "refreshToken", "idToken", "apiKey"};

static int names_a_credential(const char *name) {
    for (size_t index = 0; index < sizeof(credential_names) / sizeof(credential_names[0]); index++) {
        if (strcmp(name, credential_names[index]) == 0) {
            return 1;
        }
    }
    return 0;
}

const char *accounts_find_token_member(const cJSON *value) {
    const cJSON *child = NULL;
    cJSON_ArrayForEach(child, value) {
        if (cJSON_IsObject(value) && child->string != NULL && names_a_credential(child->string)) {
            return child->string;
        }
        const char *nested = accounts_find_token_member(child);
        if (nested != NULL) {
            return nested;
        }
    }
    return NULL;
}

int32_t accounts_refuse_token_member(const char *subject, const char *member, uint8_t **out, size_t *out_len) {
    char cause[200];
    snprintf(cause, sizeof(cause), "the %s carries the token member %s", subject, member);
    return service_refuse(cause, TOKEN_FIX, out, out_len);
}
