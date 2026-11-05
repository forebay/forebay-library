#include "routing_environment.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spisor/json_file.h"
#include "spisor/path.h"

#define LOADER_PLACEHOLDER_TOKEN "sk-ant-loader-proxy"
#define DEFAULT_SLOT "default"

static int equals_ignoring_case(const char *left, const char *right) {
    while (*left != '\0' && *right != '\0') {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right)) {
            return 0;
        }
        left++;
        right++;
    }
    return *left == *right;
}

/** The variable as the host's environment matches names: exact, and on Windows ignoring case too. */
static const char *variable(const cJSON *environment, const char *name) {
    const cJSON *entry = cJSON_GetObjectItemCaseSensitive(environment, name);
    if (!cJSON_IsString(entry) && ROUTING_WINDOWS) {
        cJSON_ArrayForEach(entry, environment) {
            if (equals_ignoring_case(entry->string, name)) {
                break;
            }
        }
    }
    return cJSON_IsString(entry) ? entry->valuestring : NULL;
}

static int has_text(const char *value) {
    if (value == NULL) {
        return 0;
    }
    for (; *value != '\0'; value++) {
        if (!isspace((unsigned char)*value)) {
            return 1;
        }
    }
    return 0;
}

static int is_own_token(const char *token) {
    if (!has_text(token)) {
        return 0;
    }
    while (isspace((unsigned char)*token)) {
        token++;
    }
    size_t length = strlen(token);
    while (length > 0 && isspace((unsigned char)token[length - 1])) {
        length--;
    }
    return !(length == strlen(LOADER_PLACEHOLDER_TOKEN) && strncmp(token, LOADER_PLACEHOLDER_TOKEN, length) == 0);
}

static int is_truthy(const cJSON *value) {
    if (value == NULL || cJSON_IsNull(value) || cJSON_IsFalse(value)) {
        return 0;
    }
    if (cJSON_IsNumber(value)) {
        return value->valuedouble != 0 && value->valuedouble == value->valuedouble;
    }
    if (cJSON_IsString(value)) {
        return value->valuestring[0] != '\0';
    }
    return 1;
}

/** Whether the application's own state file records a signed-in account. */
static int has_account_on_disk(const char *user_home, int *out_of_memory) {
    if (user_home == NULL || user_home[0] == '\0') {
        return 0;
    }
    const char *segments[] = {user_home, ".claude.json"};
    char *path = spisor_path_join(ROUTING_WINDOWS, segments, 2);
    if (path == NULL) {
        *out_of_memory = 1;
        return 0;
    }
    spisor_json_file_state state;
    cJSON *state_file = spisor_read_json_file(path, &state);
    free(path);
    int signed_in = state_file != NULL && is_truthy(cJSON_GetObjectItemCaseSensitive(state_file, "oauthAccount"));
    cJSON_Delete(state_file);
    return signed_in;
}

static int has_own_login(const cJSON *environment, const char *user_home, int *out_of_memory) {
    return has_text(variable(environment, "ANTHROPIC_API_KEY")) ||
           is_own_token(variable(environment, "ANTHROPIC_AUTH_TOKEN")) ||
           has_account_on_disk(user_home, out_of_memory);
}

static int add_variable(cJSON *environment, const char *name, const char *value) {
    return routing_add(environment, name, cJSON_CreateString(value));
}

static int add_pair(cJSON *environment, const char *prefix, const char *upper_tier, const cJSON *primary) {
    char name[512];
    const char *model = cJSON_GetObjectItemCaseSensitive(primary, "model")->valuestring;
    const cJSON *display = cJSON_GetObjectItemCaseSensitive(primary, "name");
    snprintf(name, sizeof(name), "%s_DEFAULT_%s_MODEL", prefix, upper_tier);
    if (!add_variable(environment, name, model)) {
        return 0;
    }
    snprintf(name, sizeof(name), "%s_DEFAULT_%s_MODEL_NAME", prefix, upper_tier);
    return add_variable(environment, name, cJSON_IsString(display) && display->valuestring[0] != '\0'
                                               ? display->valuestring
                                               : model);
}

static const cJSON *usable_primary(const cJSON *chain) {
    if (!cJSON_IsArray(chain) || chain->child == NULL) {
        return NULL;
    }
    const cJSON *model = cJSON_GetObjectItemCaseSensitive(chain->child, "model");
    return cJSON_IsString(model) && model->valuestring[0] != '\0' ? chain->child : NULL;
}

static int add_tier_pair(cJSON *environment, const char *prefix, const cJSON *chain) {
    const cJSON *primary = usable_primary(chain);
    if (primary == NULL || strcmp(chain->string, DEFAULT_SLOT) == 0) {
        return 1;
    }
    char upper[256];
    size_t length = strlen(chain->string);
    if (length >= sizeof(upper)) {
        length = sizeof(upper) - 1;
    }
    for (size_t index = 0; index < length; index++) {
        upper[index] = (char)toupper((unsigned char)chain->string[index]);
    }
    upper[length] = '\0';
    return add_pair(environment, prefix, upper, primary);
}

static int add_model_pairs(cJSON *environment, const char *prefix, const cJSON *map) {
    const cJSON *chain = NULL;
    cJSON_ArrayForEach(chain, map) {
        if (!add_tier_pair(environment, prefix, chain)) {
            return 0;
        }
    }
    const cJSON *primary = usable_primary(cJSON_GetObjectItemCaseSensitive(map, DEFAULT_SLOT));
    if (primary == NULL) {
        return 1;
    }
    char name[300];
    snprintf(name, sizeof(name), "%s_MODEL", prefix);
    return add_variable(environment, name, cJSON_GetObjectItemCaseSensitive(primary, "model")->valuestring);
}

typedef struct request {
    routing_profile profile;
    const cJSON *map;
    int port;
    int providers_enabled;
    const cJSON *environment;
    const char *user_home;
} request;

static cJSON *build_environment(const request *asked) {
    cJSON *built = cJSON_CreateObject();
    char base_url[64];
    snprintf(base_url, sizeof(base_url), "http://127.0.0.1:%d", asked->port);
    int ok = built != NULL && add_variable(built, "ANTHROPIC_BASE_URL", base_url);
    if (ok && asked->providers_enabled) {
        int out_of_memory = 0;
        int own_login = has_own_login(asked->environment, asked->user_home, &out_of_memory);
        ok = !out_of_memory && add_model_pairs(built, asked->profile.env_prefix, asked->map) &&
             (own_login || add_variable(built, "ANTHROPIC_AUTH_TOKEN", LOADER_PLACEHOLDER_TOKEN));
    }
    if (!ok) {
        cJSON_Delete(built);
        return NULL;
    }
    return built;
}

static int is_port(const cJSON *value) {
    return cJSON_IsNumber(value) && value->valuedouble >= 1 && value->valuedouble <= 65535 &&
           value->valuedouble == (double)(int)value->valuedouble;
}

static int read_request(const spisor_call *call, request *asked, uint8_t **out, size_t *out_len, int32_t *status) {
    const cJSON *profile = NULL;
    const cJSON *port = NULL;
    const cJSON *enabled = NULL;
    const cJSON *home = NULL;
    memset(asked, 0, sizeof(*asked));
    if (!routing_require(call, "profile", routing_is_object, "an object", &profile, out, out_len, status) ||
        !routing_require(call, "map", routing_is_object, "an object", &asked->map, out, out_len, status) ||
        !routing_require(call, "port", is_port, "a port number from 1 to 65535", &port, out, out_len, status) ||
        !routing_require(call, "providersEnabled", routing_is_boolean, "a boolean", &enabled, out, out_len,
                         status) ||
        !routing_require(call, "environment", routing_is_object, "an object", &asked->environment, out, out_len,
                         status) ||
        !routing_require(call, "userHome", routing_is_string, "a string", &home, out, out_len, status) ||
        !routing_read_profile(profile, NEEDS_ENV_PREFIX, &asked->profile, out, out_len, status)) {
        return 0;
    }
    asked->port = (int)port->valuedouble;
    asked->providers_enabled = cJSON_IsTrue(enabled);
    asked->user_home = home->valuestring;
    return 1;
}

ROUTING_EXPORT int32_t routing_environment(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len) {
    spisor_call call;
    int32_t status = routing_open(in, in_len, &call, out, out_len);
    if (status != SPISOR_STATUS_SUCCESS) {
        return status;
    }
    request asked;
    if (read_request(&call, &asked, out, out_len, &status)) {
        status = routing_answer(routing_wrap("environment", build_environment(&asked)), out, out_len);
    }
    spisor_discard_call(&call);
    return status;
}
