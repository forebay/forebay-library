#include "accounts_quota.h"

#include "accounts_support.h"

/** QuotaHealth.hasCapacity: a pool counts when its remainingFraction is a number above zero. */
static int has_remaining(const cJSON *pool) {
    const cJSON *fraction = cJSON_GetObjectItemCaseSensitive(pool, "remainingFraction");
    return cJSON_IsObject(pool) && cJSON_IsNumber(fraction) && fraction->valuedouble > 0;
}

static int any_pool_has_capacity(const cJSON *pools) {
    const cJSON *pool = NULL;
    cJSON_ArrayForEach(pool, pools) {
        if (has_remaining(pool)) {
            return 1;
        }
    }
    return 0;
}

SERVICE_EXPORT int32_t accounts_has_capacity(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len) {
    spisor_call call;
    int32_t status = service_open(in, in_len, &call, out, out_len);
    if (status != SPISOR_STATUS_SUCCESS) {
        return status;
    }
    const cJSON *pools = NULL;
    if (service_require(&call, "pools", service_is_array, "an array", &pools, out, out_len, &status)) {
        cJSON *answer = cJSON_CreateObject();
        if (answer != NULL && cJSON_AddBoolToObject(answer, "capacity", any_pool_has_capacity(pools)) == NULL) {
            cJSON_Delete(answer);
            answer = NULL;
        }
        status = service_answer(answer, out, out_len);
    }
    spisor_discard_call(&call);
    return status;
}
