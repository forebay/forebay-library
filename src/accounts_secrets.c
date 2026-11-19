#include "accounts_secrets.h"

#include "definition_support.h"

#include <stdio.h>
#include <string.h>

#define TOKEN_FIX "keep tokens in the plugin's secrets and store only non-secret account state"

static const char *const credential_names[] = {"access", "refresh", "accessToken", "refreshToken", "idToken", "apiKey"};

/** The credential name `key` spells exactly, or NULL. */
static const char *credential_named(const char *key, size_t length) {
    for (size_t index = 0; index < sizeof(credential_names) / sizeof(credential_names[0]); index++) {
        if (strlen(credential_names[index]) == length && memcmp(credential_names[index], key, length) == 0) {
            return credential_names[index];
        }
    }
    return NULL;
}

static const char *find_credential(const spisor_value *value) {
    const char *found = NULL;
    for (size_t index = 0; found == NULL && value->kind == SPISOR_VALUE_LIST && index < value->count; index++) {
        found = find_credential(&value->items[index]);
    }
    for (size_t index = 0; found == NULL && value->kind == SPISOR_VALUE_RECORD && index < value->count; index++) {
        const spisor_entry *entry = &value->entries[index];
        found = credential_named(entry->key, entry->key_length);
        if (found == NULL) {
            found = find_credential(&entry->value);
        }
    }
    return found;
}

spisor_status accounts_refuse_credentials(const spisor_value *value, const char *subject, spisor_refusal *refusal) {
    spisor_status status = spisor_value_check(value);
    if (status == SPISOR_MALFORMED) {
        return forebay_refuse_member(refusal, subject, "a well-formed spisor_value");
    }
    const char *member = status == SPISOR_OK ? find_credential(value) : NULL;
    if (member == NULL) {
        return status;
    }
    char cause[200];
    snprintf(cause, sizeof(cause), "the %s carries the token member %s", subject, member);
    return spisor_refuse_as(refusal, SPISOR_REFUSED, cause, TOKEN_FIX);
}
