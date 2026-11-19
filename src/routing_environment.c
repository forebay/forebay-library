#include "routing_environment.h"

#include "spisor/allocation.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spisor/json_file.h"
#include "spisor/path.h"

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
    if (!cJSON_IsString(entry) && SERVICE_WINDOWS) {
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

static int is_own_token(const char *token, const char *placeholder) {
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
    return !(length == strlen(placeholder) && strncmp(token, placeholder, length) == 0);
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

static const char *text_member(const cJSON *object, const char *name) {
    const cJSON *found = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(found) ? found->valuestring : NULL;
}

/**
 * Whether the application's declared state file records a signed-in account.
 *
 * @implNote A set directoryVariable replaces the user's home rather than adding a second place to look.
 */
static int has_account_on_disk(const cJSON *environment, const char *user_home, const cJSON *state_file_facts,
                               int *out_of_memory) {
    if (state_file_facts == NULL) {
        return 0;
    }
    const char *directory_variable = text_member(state_file_facts, "directoryVariable");
    const char *config_directory = directory_variable == NULL ? NULL : variable(environment, directory_variable);
    const char *directory = has_text(config_directory) ? config_directory : user_home;
    if (directory == NULL || directory[0] == '\0') {
        return 0;
    }
    const char *segments[] = {directory, text_member(state_file_facts, "name")};
    char *path = spisor_path_join(SERVICE_WINDOWS, segments, 2);
    if (path == NULL) {
        *out_of_memory = 1;
        return 0;
    }
    spisor_json_file_state state = SPISOR_JSON_FILE_UNREADABLE;
    cJSON *state_file = spisor_read_json_file(path, &state);
    spisor_memory_release(path);
    int signed_in = state_file != NULL &&
                    is_truthy(cJSON_GetObjectItemCaseSensitive(state_file, text_member(state_file_facts, "member")));
    cJSON_Delete(state_file);
    return signed_in;
}

static int any_variable_has_text(const cJSON *environment, const cJSON *names) {
    const cJSON *name;
    cJSON_ArrayForEach(name, names) {
        if (has_text(variable(environment, name->valuestring))) {
            return 1;
        }
    }
    return 0;
}

static int has_own_login(const cJSON *environment, const char *user_home, const cJSON *application,
                         int *out_of_memory) {
    const cJSON *placeholder = cJSON_GetObjectItemCaseSensitive(application, "placeholder");
    const cJSON *own_login = cJSON_GetObjectItemCaseSensitive(application, "ownLogin");
    return any_variable_has_text(environment, cJSON_GetObjectItemCaseSensitive(own_login, "variables")) ||
           is_own_token(variable(environment, text_member(placeholder, "variable")),
                        text_member(placeholder, "value")) ||
           has_account_on_disk(environment, user_home, cJSON_GetObjectItemCaseSensitive(own_login, "stateFile"),
                               out_of_memory);
}

static int add_variable(cJSON *environment, const char *name, const char *value) {
    return service_add(environment, name, cJSON_CreateString(value));
}

#define TIER_PLACEHOLDER "{{TIER}}"

static int expand_tier(const char *name_template, const char *upper_tier, char *name, size_t capacity) {
    size_t written = 0;
    const char *rest = name_template;
    for (const char *at = strstr(rest, TIER_PLACEHOLDER); at != NULL; at = strstr(rest, TIER_PLACEHOLDER)) {
        int length = snprintf(name + written, capacity - written, "%.*s%s", (int)(at - rest), rest, upper_tier);
        if (length < 0 || (size_t)length >= capacity - written) {
            return 0;
        }
        written += (size_t)length;
        rest = at + strlen(TIER_PLACEHOLDER);
    }
    return (size_t)snprintf(name + written, capacity - written, "%s", rest) < capacity - written;
}

static int add_tier_variable(cJSON *environment, const char *name_template, const char *upper_tier, const char *value) {
    char name[512];
    return name_template == NULL ||
           (expand_tier(name_template, upper_tier, name, sizeof(name)) && add_variable(environment, name, value));
}

static int add_pair(cJSON *environment, const cJSON *variables, const char *upper_tier, const cJSON *primary) {
    const char *model = cJSON_GetObjectItemCaseSensitive(primary, "model")->valuestring;
    const cJSON *display = cJSON_GetObjectItemCaseSensitive(primary, "name");
    const char *label = cJSON_IsString(display) && display->valuestring[0] != '\0' ? display->valuestring : model;
    return add_tier_variable(environment, text_member(variables, "tierModel"), upper_tier, model) &&
           add_tier_variable(environment, text_member(variables, "tierModelName"), upper_tier, label);
}

static const cJSON *usable_primary(const cJSON *chain) {
    if (!cJSON_IsArray(chain) || chain->child == NULL) {
        return NULL;
    }
    const cJSON *model = cJSON_GetObjectItemCaseSensitive(chain->child, "model");
    return cJSON_IsString(model) && model->valuestring[0] != '\0' ? chain->child : NULL;
}

static int add_tier_pair(cJSON *environment, const cJSON *variables, const cJSON *chain) {
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
    return add_pair(environment, variables, upper, primary);
}

static int add_model_pairs(cJSON *environment, const cJSON *variables, const cJSON *map) {
    const cJSON *chain = NULL;
    cJSON_ArrayForEach(chain, map) {
        if (!add_tier_pair(environment, variables, chain)) {
            return 0;
        }
    }
    const char *default_variable = text_member(variables, "model");
    const cJSON *primary = usable_primary(cJSON_GetObjectItemCaseSensitive(map, DEFAULT_SLOT));
    if (primary == NULL || default_variable == NULL) {
        return 1;
    }
    return add_variable(environment, default_variable, cJSON_GetObjectItemCaseSensitive(primary, "model")->valuestring);
}

typedef struct request {
    const cJSON *map;
    int port;
    int providers_enabled;
    const cJSON *environment;
    const char *user_home;
    const cJSON *application;
} request;

static cJSON *build_environment(const request *asked) {
    cJSON *built = cJSON_CreateObject();
    char base_url[64];
    snprintf(base_url, sizeof(base_url), "http://127.0.0.1:%d", asked->port);
    const cJSON *placeholder = cJSON_GetObjectItemCaseSensitive(asked->application, "placeholder");
    int ok = built != NULL && add_variable(built, text_member(asked->application, "baseUrlVariable"), base_url);
    if (ok && asked->providers_enabled) {
        int out_of_memory = 0;
        int own_login = has_own_login(asked->environment, asked->user_home, asked->application, &out_of_memory);
        ok =
            !out_of_memory &&
            add_model_pairs(built, cJSON_GetObjectItemCaseSensitive(asked->application, "modelVariables"),
                            asked->map) &&
            (own_login || add_variable(built, text_member(placeholder, "variable"), text_member(placeholder, "value")));
    }
    if (!ok) {
        cJSON_Delete(built);
        return NULL;
    }
    return built;
}

static int refuses_tier_template(const cJSON *variables, const char *member, const char *field,
                                  service_reply *reply, spisor_status *status) {
    const char *name_template = text_member(variables, member);
    if (name_template == NULL || strstr(name_template, TIER_PLACEHOLDER) != NULL) {
        return 0;
    }
    char cause[96];
    char fix[128];
    snprintf(cause, sizeof(cause), "the application's %s lacks the " TIER_PLACEHOLDER " placeholder", field);
    snprintf(fix, sizeof(fix), "include " TIER_PLACEHOLDER " in the application's %s where the upper-cased tier goes",
             field);
    *status = service_refuse(cause, fix, reply);
    return 1;
}

static int read_request(const service_call *call, request *asked, service_reply *reply, spisor_status *status) {
    memset(asked, 0, sizeof(*asked));
    asked->map = cJSON_GetObjectItemCaseSensitive(call->argument, "map");
    asked->environment = cJSON_GetObjectItemCaseSensitive(call->argument, "environment");
    asked->application = cJSON_GetObjectItemCaseSensitive(call->argument, "application");
    const cJSON *variables = cJSON_GetObjectItemCaseSensitive(asked->application, "modelVariables");
    if (refuses_tier_template(variables, "tierModel", "tier_model_variable", reply, status) ||
        refuses_tier_template(variables, "tierModelName", "tier_model_name_variable", reply, status)) {
        return 0;
    }
    asked->port = (int)cJSON_GetObjectItemCaseSensitive(call->argument, "port")->valuedouble;
    asked->providers_enabled = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(call->argument, "providersEnabled"));
    asked->user_home = cJSON_GetObjectItemCaseSensitive(call->argument, "userHome")->valuestring;
    return 1;
}

spisor_status routing_environment_call(const cJSON *argument, service_reply *reply) {
    service_call call = {argument};
    spisor_status status = SPISOR_OK;
    request asked;
    if (read_request(&call, &asked, reply, &status)) {
        status = service_answer(service_wrap("environment", build_environment(&asked)), reply);
    }
    return status;
}
