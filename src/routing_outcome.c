#include "routing_outcome.h"

#include <ctype.h>
#include <stdint.h>

#define RATE_LIMITED_STATUS 429
#define MAX_JAVA_INT INT64_C(2147483647)

/**
 * Java's lenient integer read: trimmed, an optional sign, then a run of digits with trailing text
 * ignored. Anything unreadable, or beyond a 32-bit int, answers 0, which callers treat as absent.
 */
static int64_t lenient_integer(const char *text) {
    if (text == NULL) {
        return 0;
    }
    while (*text != '\0' && (unsigned char)*text <= ' ') {
        text++;
    }
    int negative = *text == '-';
    if (*text == '+' || *text == '-') {
        text++;
    }
    if (!isdigit((unsigned char)*text)) {
        return 0;
    }
    int64_t value = 0;
    while (isdigit((unsigned char)*text)) {
        value = value * 10 + (*text - '0');
        if (value > MAX_JAVA_INT + (negative ? 1 : 0)) {
            return 0;
        }
        text++;
    }
    return negative ? -value : value;
}

static int is_rate_limited(double status) {
    return status == RATE_LIMITED_STATUS;
}

/** Epoch milliseconds the response says it is usable again, or 0 when it names none. */
static double reset_at(const cJSON *argument, const cJSON *headers, double now) {
    const cJSON *milliseconds = cJSON_GetObjectItemCaseSensitive(argument, "retryAfterMilliseconds");
    if (cJSON_IsNumber(milliseconds) && milliseconds->valuedouble > 0) {
        return now + milliseconds->valuedouble;
    }
    int64_t seconds = lenient_integer(routing_header(headers, "retry-after"));
    if (seconds > 0) {
        return now + (double)seconds * 1000.0;
    }
    return 0;
}

cJSON *routing_outcome_of(const cJSON *argument) {
    cJSON *result = cJSON_CreateObject();
    double status = cJSON_GetObjectItemCaseSensitive(argument, "status")->valuedouble;
    if (result == NULL || cJSON_AddBoolToObject(result, "rateLimited", is_rate_limited(status)) == NULL) {
        cJSON_Delete(result);
        return NULL;
    }
    double reset = reset_at(argument, cJSON_GetObjectItemCaseSensitive(argument, "headers"),
                            cJSON_GetObjectItemCaseSensitive(argument, "now")->valuedouble);
    if (reset > 0 && cJSON_AddNumberToObject(result, "resetAt", reset) == NULL) {
        cJSON_Delete(result);
        return NULL;
    }
    return result;
}

int32_t routing_outcome_call(const cJSON *argument, service_reply *reply) {
    service_call call = {argument};
    int32_t status = SERVICE_SUCCESS;
    const cJSON *reply_status = NULL;
    const cJSON *headers = NULL;
    const cJSON *now = NULL;
    if (service_require(&call, "status", service_is_number, "a number", &reply_status, reply, &status) &&
        service_require(&call, "headers", service_is_object, "an object", &headers, reply, &status) &&
        service_require(&call, "now", service_is_number, "a number", &now, reply, &status)) {
        status = service_answer(routing_outcome_of(call.argument), reply);
    }
    return status;
}
