#include "apps_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define APPS_EXPORT __declspec(dllexport)
#else
#define APPS_EXPORT __attribute__((visibility("default")))
#endif

typedef enum fact_need { NEEDS_HOME_AND_ENVIRONMENT, NEEDS_ARGUMENTS_TOO } fact_need;

typedef struct apps_request {
    spisor_call call;
    apps_facts facts;
    const char *id;
} apps_request;

static int32_t out_of_memory(uint8_t **out, size_t *out_len) {
    *out = NULL;
    *out_len = 0;
    return SPISOR_STATUS_OUT_OF_MEMORY;
}

static int32_t refuse_missing_fact(const char *fact, uint8_t **out, size_t *out_len) {
    char message[64];
    char fix[96];
    snprintf(message, sizeof(message), "the context carries no %s", fact);
    snprintf(fix, sizeof(fix), "declare %s in this function's context so the host sends it", fact);
    return spisor_fail(message, fix, out, out_len);
}

static int32_t gather_facts(apps_request *request, fact_need need, uint8_t **out, size_t *out_len) {
    const cJSON *context = request->call.context;
    request->facts.call = &request->call;
    request->facts.home = spisor_context_string(&request->call, "home");
    request->facts.arguments = cJSON_GetObjectItemCaseSensitive(context, "arguments");
    if (request->facts.home == NULL) {
        return refuse_missing_fact("home", out, out_len);
    }
    if (cJSON_GetObjectItemCaseSensitive(context, "environment") == NULL) {
        return refuse_missing_fact("environment", out, out_len);
    }
    if (need == NEEDS_ARGUMENTS_TOO && request->facts.arguments == NULL) {
        return refuse_missing_fact("arguments", out, out_len);
    }
    return SPISOR_STATUS_SUCCESS;
}

static int32_t open_request(const uint8_t *in, size_t in_len, fact_need need, int takes_id, apps_request *request,
                            uint8_t **out, size_t *out_len) {
    memset(request, 0, sizeof(*request));
    int32_t status = spisor_parse_call(in, in_len, 1, &request->call, out, out_len);
    if (status == SPISOR_STATUS_SUCCESS && !cJSON_IsObject(request->call.argument)) {
        status = spisor_fail("the argument is not an object", "send the argument as a JSON object", out, out_len);
    }
    if (status == SPISOR_STATUS_SUCCESS && takes_id) {
        request->id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(request->call.argument, "id"));
        if (request->id == NULL) {
            status = spisor_fail("the argument carries no string id", "send {\"id\": \"<app id>\"}", out, out_len);
        }
    }
    if (status == SPISOR_STATUS_SUCCESS) {
        status = gather_facts(request, need, out, out_len);
    }
    return status;
}

static int32_t answer_raised(apps_error *error, uint8_t **out, size_t *out_len) {
    int32_t status = error->out_of_memory ? out_of_memory(out, out_len)
                                          : spisor_fail(error->message, error->fix, out, out_len);
    apps_clear_error(error);
    return status;
}

/** Answers {field: value} with value consumed, or {} when value is NULL. */
static int32_t answer_with(const char *field, cJSON *value, uint8_t **out, size_t *out_len) {
    cJSON *result = cJSON_CreateObject();
    if (result == NULL || (value != NULL && !cJSON_AddItemToObject(result, field, value))) {
        cJSON_Delete(result);
        cJSON_Delete(value);
        return out_of_memory(out, out_len);
    }
    int32_t status = spisor_succeed(result, out, out_len);
    cJSON_Delete(result);
    return status;
}

static cJSON *detach_app(cJSON *apps, const char *id) {
    cJSON *app = NULL;
    cJSON_ArrayForEach(app, apps) {
        const char *app_id = apps_descriptor_id(app);
        if (app_id != NULL && strcmp(app_id, id) == 0) {
            return cJSON_DetachItemViaPointer(apps, app);
        }
    }
    return NULL;
}

static int32_t list(apps_request *request, uint8_t **out, size_t *out_len) {
    apps_error error = {0, 0, NULL, NULL};
    cJSON *apps = apps_build(&request->facts, &error);
    return apps == NULL ? answer_raised(&error, out, out_len) : answer_with("apps", apps, out, out_len);
}

static int32_t get(apps_request *request, uint8_t **out, size_t *out_len) {
    apps_error error = {0, 0, NULL, NULL};
    cJSON *apps = apps_build(&request->facts, &error);
    if (apps == NULL) {
        return answer_raised(&error, out, out_len);
    }
    cJSON *app = detach_app(apps, request->id);
    cJSON_Delete(apps);
    return answer_with("app", app, out, out_len);
}

static int32_t home(apps_request *request, uint8_t **out, size_t *out_len) {
    apps_error error = {0, 0, NULL, NULL};
    cJSON *apps = apps_build(&request->facts, &error);
    if (apps == NULL) {
        return answer_raised(&error, out, out_len);
    }
    cJSON *app = detach_app(apps, request->id);
    cJSON_Delete(apps);
    if (app == NULL) {
        return answer_with("home", NULL, out, out_len);
    }
    char *resolved = apps_resolve_home(app, &request->facts, &error);
    cJSON_Delete(app);
    if (resolved == NULL) {
        return answer_raised(&error, out, out_len);
    }
    cJSON *value = cJSON_CreateString(resolved);
    free(resolved);
    return value == NULL ? out_of_memory(out, out_len) : answer_with("home", value, out, out_len);
}

static int32_t current(apps_request *request, uint8_t **out, size_t *out_len) {
    apps_error error = {0, 0, NULL, NULL};
    char *id = apps_current_id(&request->facts, &error);
    if (id == NULL) {
        return answer_raised(&error, out, out_len);
    }
    cJSON *value = cJSON_CreateString(id);
    free(id);
    return value == NULL ? out_of_memory(out, out_len) : answer_with("id", value, out, out_len);
}

typedef int32_t (*apps_handler)(apps_request *request, uint8_t **out, size_t *out_len);

static int32_t serve(const uint8_t *in, size_t in_len, fact_need need, int takes_id, apps_handler handler,
                     uint8_t **out, size_t *out_len) {
    apps_request request;
    int32_t status = open_request(in, in_len, need, takes_id, &request, out, out_len);
    if (status == SPISOR_STATUS_SUCCESS) {
        status = handler(&request, out, out_len);
    }
    spisor_discard_call(&request.call);
    return status;
}

APPS_EXPORT int32_t forebay_library_apps_list(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len) {
    return serve(in, in_len, NEEDS_HOME_AND_ENVIRONMENT, 0, list, out, out_len);
}

APPS_EXPORT int32_t forebay_library_apps_get(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len) {
    return serve(in, in_len, NEEDS_HOME_AND_ENVIRONMENT, 1, get, out, out_len);
}

APPS_EXPORT int32_t forebay_library_apps_home(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len) {
    return serve(in, in_len, NEEDS_HOME_AND_ENVIRONMENT, 1, home, out, out_len);
}

APPS_EXPORT int32_t forebay_library_apps_current(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len) {
    return serve(in, in_len, NEEDS_ARGUMENTS_TOO, 0, current, out, out_len);
}

APPS_EXPORT void forebay_library_release(uint8_t *buffer) {
    spisor_release(buffer);
}
