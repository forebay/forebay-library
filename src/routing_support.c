#include "routing_support.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int32_t routing_answer_refusal(const routing_refusal *refusal, uint8_t **out, size_t *out_len) {
    if (!refusal->refused) {
        return service_out_of_memory(out, out_len);
    }
    return service_refuse(refusal->cause, refusal->fix, out, out_len);
}

static int all_strings(const cJSON *list) {
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, list) {
        if (!cJSON_IsString(item)) {
            return 0;
        }
    }
    return 1;
}

static int profile_string(const cJSON *profile, const char *name, const char **value, uint8_t **out, size_t *out_len,
                          int32_t *status) {
    const cJSON *member = cJSON_GetObjectItemCaseSensitive(profile, name);
    if (!cJSON_IsString(member)) {
        *status = service_fail("the profile member", name, "send the profile with this member as a string", out,
                               out_len);
        return 0;
    }
    *value = member->valuestring;
    return 1;
}

static int profile_list(const cJSON *profile, const char *name, const cJSON **value, uint8_t **out, size_t *out_len,
                        int32_t *status) {
    const cJSON *member = cJSON_GetObjectItemCaseSensitive(profile, name);
    if (!cJSON_IsArray(member) || !all_strings(member)) {
        *status = service_fail("the profile member", name, "send the profile with this member as a list of strings",
                               out, out_len);
        return 0;
    }
    *value = member;
    return 1;
}

static int read_tier_members(const cJSON *profile, routing_profile *read, uint8_t **out, size_t *out_len,
                             int32_t *status) {
    return profile_string(profile, "tierSourceProvider", &read->tier_source_provider, out, out_len, status) &&
           profile_list(profile, "tierOrder", &read->tier_order, out, out_len, status) &&
           profile_list(profile, "tierFallback", &read->tier_fallback, out, out_len, status) &&
           profile_string(profile, "tierRegex", &read->tier_regex, out, out_len, status);
}

static int read_native_pattern(const cJSON *profile, routing_profile *read, uint8_t **out, size_t *out_len,
                               int32_t *status) {
    const cJSON *member = cJSON_GetObjectItemCaseSensitive(profile, "nativeModelPattern");
    if (member == NULL || cJSON_IsNull(member)) {
        read->native_model_pattern = NULL;
        return 1;
    }
    return profile_string(profile, "nativeModelPattern", &read->native_model_pattern, out, out_len, status);
}

static int read_unserved_pattern(const cJSON *profile, routing_profile *read, uint8_t **out, size_t *out_len,
                                 int32_t *status) {
    const cJSON *member = cJSON_GetObjectItemCaseSensitive(profile, "unservedModelPattern");
    if (member == NULL || cJSON_IsNull(member)) {
        read->unserved_model_pattern = NULL;
        return 1;
    }
    return profile_string(profile, "unservedModelPattern", &read->unserved_model_pattern, out, out_len, status);
}

int routing_read_profile(const cJSON *profile, int need, routing_profile *read, uint8_t **out, size_t *out_len,
                         int32_t *status) {
    memset(read, 0, sizeof(*read));
    return (!(need & NEEDS_TIERS) || read_tier_members(profile, read, out, out_len, status)) &&
           (!(need & NEEDS_NATIVE_PATTERN) || read_native_pattern(profile, read, out, out_len, status)) &&
           (!(need & NEEDS_UNSERVED_PATTERN) || read_unserved_pattern(profile, read, out, out_len, status));
}

cJSON *routing_assignment(const char *provider, const char *model, const char *name, int derived) {
    cJSON *assignment = cJSON_CreateObject();
    if (assignment == NULL || cJSON_AddStringToObject(assignment, "provider", provider) == NULL ||
        cJSON_AddStringToObject(assignment, "model", model) == NULL ||
        cJSON_AddStringToObject(assignment, "name", name) == NULL ||
        cJSON_AddBoolToObject(assignment, "derived", derived) == NULL) {
        cJSON_Delete(assignment);
        return NULL;
    }
    return assignment;
}

static int is_usable_text(const cJSON *value) {
    return cJSON_IsString(value) && value->valuestring[0] != '\0';
}

static int is_valid_entry(const cJSON *raw) {
    return cJSON_IsObject(raw) && is_usable_text(cJSON_GetObjectItemCaseSensitive(raw, "provider")) &&
           is_usable_text(cJSON_GetObjectItemCaseSensitive(raw, "model"));
}

static cJSON *assignment_from(const cJSON *raw) {
    const cJSON *provider = cJSON_GetObjectItemCaseSensitive(raw, "provider");
    const cJSON *model = cJSON_GetObjectItemCaseSensitive(raw, "model");
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(raw, "name");
    int derived = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(raw, "derived"));
    return routing_assignment(provider->valuestring, model->valuestring,
                              is_usable_text(name) ? name->valuestring : model->valuestring, derived);
}

static int append_if_valid(cJSON *chain, const cJSON *entry) {
    return !is_valid_entry(entry) || service_push(chain, assignment_from(entry));
}

cJSON *routing_normalize_chain(const cJSON *raw) {
    cJSON *chain = cJSON_CreateArray();
    if (chain == NULL) {
        return NULL;
    }
    int complete = 1;
    if (cJSON_IsArray(raw)) {
        const cJSON *entry = NULL;
        cJSON_ArrayForEach(entry, raw) {
            complete = complete && append_if_valid(chain, entry);
        }
    } else if (raw != NULL) {
        complete = append_if_valid(chain, raw);
    }
    if (!complete) {
        cJSON_Delete(chain);
        return NULL;
    }
    return chain;
}

cJSON *routing_ranked_ids(const cJSON *provider_entry) {
    cJSON *ids = cJSON_CreateArray();
    if (ids == NULL) {
        return NULL;
    }
    const cJSON *ranking = cJSON_GetObjectItemCaseSensitive(provider_entry, "ranking");
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, ranking) {
        if (cJSON_IsString(item) && !service_push(ids, cJSON_CreateString(item->valuestring))) {
            cJSON_Delete(ids);
            return NULL;
        }
    }
    if (cJSON_GetArraySize(ids) > 0) {
        return ids;
    }
    const cJSON *models = cJSON_GetObjectItemCaseSensitive(provider_entry, "models");
    if (cJSON_IsObject(models)) {
        cJSON_ArrayForEach(item, models) {
            if (!service_push(ids, cJSON_CreateString(item->string))) {
                cJSON_Delete(ids);
                return NULL;
            }
        }
    }
    return ids;
}

static const char *display_name(const cJSON *raw, const char *model) {
    if (cJSON_IsObject(raw)) {
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(raw, "name");
        if (cJSON_IsString(name) && name->valuestring[0] != '\0') {
            return name->valuestring;
        }
    }
    return model;
}

static int add_provider_models(cJSON *catalog, const cJSON *provider_entry, const char *provider,
                               const routing_pattern *unserved) {
    const cJSON *models = cJSON_GetObjectItemCaseSensitive(provider_entry, "models");
    if (!cJSON_IsObject(provider_entry) || !cJSON_IsObject(models)) {
        return 1;
    }
    cJSON *ids = routing_ranked_ids(provider_entry);
    if (ids == NULL) {
        return 0;
    }
    const cJSON *id = NULL;
    cJSON_ArrayForEach(id, ids) {
        const cJSON *raw = cJSON_GetObjectItemCaseSensitive(models, id->valuestring);
        if (raw == NULL || cJSON_IsNull(raw) || (unserved != NULL && routing_pattern_find(unserved, id->valuestring, NULL, NULL))) {
            continue;
        }
        if (!service_push(catalog, routing_assignment(provider, id->valuestring,
                                                      display_name(raw, id->valuestring), 0))) {
            cJSON_Delete(ids);
            return 0;
        }
    }
    cJSON_Delete(ids);
    return 1;
}

cJSON *routing_catalog(const cJSON *models, const routing_pattern *unserved) {
    cJSON *catalog = cJSON_CreateArray();
    if (catalog == NULL) {
        return NULL;
    }
    const cJSON *provider_entry = NULL;
    cJSON_ArrayForEach(provider_entry, models) {
        if (!add_provider_models(catalog, provider_entry, provider_entry->string, unserved)) {
            cJSON_Delete(catalog);
            return NULL;
        }
    }
    return catalog;
}

routing_pattern *routing_compile_profile_pattern(const char *member, const char *source, int group_required,
                                                 routing_refusal *refusal) {
    routing_pattern *pattern = NULL;
    char reason[160];
    routing_pattern_status status = routing_pattern_compile(source, &pattern, reason, sizeof(reason));
    refusal->refused = 0;
    if (status == ROUTING_PATTERN_OUT_OF_MEMORY) {
        return NULL;
    }
    refusal->refused = 1;
    refusal->fix = "write the pattern with only ^ $ literals . [] \\d \\w \\s * + ? and at most one capture group";
    if (status == ROUTING_PATTERN_UNSUPPORTED) {
        snprintf(refusal->cause, sizeof(refusal->cause), "%s uses an unsupported construct: %s", member, reason);
        return NULL;
    }
    if (group_required && routing_pattern_group_count(pattern) != 1) {
        snprintf(refusal->cause, sizeof(refusal->cause), "%s must contain exactly one capture group", member);
        routing_pattern_free(pattern);
        return NULL;
    }
    refusal->refused = 0;
    return pattern;
}

int routing_compile_unserved_pattern(const routing_profile *profile, routing_pattern **pattern,
                                     routing_refusal *refusal) {
    *pattern = NULL;
    refusal->refused = 0;
    if (profile->unserved_model_pattern == NULL || profile->unserved_model_pattern[0] == '\0') {
        return 1;
    }
    *pattern = routing_compile_profile_pattern("unservedModelPattern", profile->unserved_model_pattern, 0, refusal);
    return *pattern != NULL;
}

int routing_equals_ignoring_case(const char *left, const char *right) {
    while (*left != '\0' && *right != '\0') {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right)) {
            return 0;
        }
        left++;
        right++;
    }
    return *left == *right;
}

int routing_starts_with_ignoring_case(const char *text, const char *prefix) {
    while (*prefix != '\0') {
        if (*text == '\0' || tolower((unsigned char)*text) != tolower((unsigned char)*prefix)) {
            return 0;
        }
        text++;
        prefix++;
    }
    return 1;
}

const char *routing_header(const cJSON *headers, const char *name) {
    const cJSON *direct = cJSON_GetObjectItemCaseSensitive(headers, name);
    if (cJSON_IsString(direct)) {
        return direct->valuestring;
    }
    const cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, headers) {
        if (cJSON_IsString(entry) && routing_equals_ignoring_case(entry->string, name)) {
            return entry->valuestring;
        }
    }
    return NULL;
}

int routing_is_port(const cJSON *value) {
    return cJSON_IsNumber(value) && value->valuedouble >= 1 && value->valuedouble <= 65535 &&
           value->valuedouble == (double)(int)value->valuedouble;
}

char *routing_formatted_list(const char *format, va_list values) {
    va_list copy;
    va_copy(copy, values);
    int length = vsnprintf(NULL, 0, format, values);
    char *text = length < 0 ? NULL : malloc((size_t)length + 1);
    if (text != NULL) {
        vsnprintf(text, (size_t)length + 1, format, copy);
    }
    va_end(copy);
    return text;
}

char *routing_formatted(const char *format, ...) {
    va_list values;
    va_start(values, format);
    char *text = routing_formatted_list(format, values);
    va_end(values);
    return text;
}
