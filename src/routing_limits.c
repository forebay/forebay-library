#include "routing_limits.h"

#include <stdio.h>

#define TOO_LARGE_STATUS 413
#define MODELS_BUDGET_MILLISECONDS 2000.0
#define REDERIVE_INTERVAL_MILLISECONDS 3000.0
#define REFRESH_EVERY_MILLISECONDS (60.0 * 60.0 * 1000.0)

/** Set above what any upstream accepts in one request, so the cap never refuses what an upstream would serve. */
#define MAX_REQUEST_BYTES (64.0 * 1024.0 * 1024.0)

static cJSON *too_large_reply(void) {
    char message[128];
    snprintf(message, sizeof(message), "the request body is larger than the %.0f bytes the proxy accepts",
             MAX_REQUEST_BYTES);
    cJSON *reply = cJSON_CreateObject();
    if (reply == NULL || cJSON_AddStringToObject(reply, "kind", "proxyError") == NULL ||
        cJSON_AddNumberToObject(reply, "status", TOO_LARGE_STATUS) == NULL ||
        cJSON_AddStringToObject(reply, "message", message) == NULL) {
        cJSON_Delete(reply);
        return NULL;
    }
    return reply;
}

static int add_timings(cJSON *result) {
    return cJSON_AddNumberToObject(result, "modelsBudgetMilliseconds", MODELS_BUDGET_MILLISECONDS) != NULL &&
           cJSON_AddNumberToObject(result, "rederiveIntervalMilliseconds", REDERIVE_INTERVAL_MILLISECONDS) != NULL &&
           cJSON_AddNumberToObject(result, "refreshEveryMilliseconds", REFRESH_EVERY_MILLISECONDS) != NULL;
}

static cJSON *limits_answer(void) {
    cJSON *result = cJSON_CreateObject();
    if (result == NULL || cJSON_AddNumberToObject(result, "maxRequestBytes", MAX_REQUEST_BYTES) == NULL ||
        !service_add(result, "tooLarge", too_large_reply()) || !add_timings(result)) {
        cJSON_Delete(result);
        return NULL;
    }
    return result;
}

SERVICE_EXPORT int32_t forebay_library_routing_limits(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len) {
    spisor_call call;
    int32_t status = service_open(in, in_len, &call, out, out_len);
    if (status != SPISOR_STATUS_SUCCESS) {
        return status;
    }
    status = service_answer(limits_answer(), out, out_len);
    spisor_discard_call(&call);
    return status;
}
