#include "accounts_token.h"

#include "accounts_support.h"

#define DEFAULT_SKEW_MILLISECONDS 60000.0

/**
 * TokenRefresh.accessTokenExpired for an expiry the provider read from its secrets: no expiry is expired,
 * and so is one within skew of now (<=, so the edge counts as expired).
 */
static int is_expired(const cJSON *expires, double now, double skew) {
    return expires == NULL || expires->valuedouble <= now + skew;
}

static int read_skew(const spisor_call *call, double *skew, uint8_t **out, size_t *out_len, int32_t *status) {
    int present = 0;
    *skew = DEFAULT_SKEW_MILLISECONDS;
    if (!accounts_optional_number(call, "skewMilliseconds", skew, &present, out, out_len, status)) {
        return 0;
    }
    if (*skew < 0) {
        *status = service_fail("skewMilliseconds", "is negative", "send a number of milliseconds from 0", out, out_len);
        return 0;
    }
    return 1;
}

SERVICE_EXPORT int32_t accounts_token_expired(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len) {
    spisor_call call;
    int32_t status = service_open(in, in_len, &call, out, out_len);
    if (status != SPISOR_STATUS_SUCCESS) {
        return status;
    }
    const cJSON *expires = NULL;
    const cJSON *now = NULL;
    double skew = 0;
    if (accounts_optional(&call, "expires", service_is_number, "a number", &expires, out, out_len, &status) &&
        service_require(&call, "now", service_is_number, "a number", &now, out, out_len, &status) &&
        read_skew(&call, &skew, out, out_len, &status)) {
        cJSON *answer = cJSON_CreateObject();
        if (answer != NULL && cJSON_AddBoolToObject(answer, "expired", is_expired(expires, now->valuedouble, skew)) == NULL) {
            cJSON_Delete(answer);
            answer = NULL;
        }
        status = service_answer(answer, out, out_len);
    }
    spisor_discard_call(&call);
    return status;
}
