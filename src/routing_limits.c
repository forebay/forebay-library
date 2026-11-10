#include "routing_limits.h"

/** Set above what any upstream accepts in one request, so the cap never refuses what an upstream would serve. */
#define MAX_REQUEST_BYTES (64.0 * 1024.0 * 1024.0)

static cJSON *limits_answer(void) {
    cJSON *result = cJSON_CreateObject();
    if (result == NULL || cJSON_AddNumberToObject(result, "maxRequestBytes", MAX_REQUEST_BYTES) == NULL) {
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
