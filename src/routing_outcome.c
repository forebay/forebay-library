#include "routing_outcome.h"

#include <ctype.h>
#include <stdint.h>

#define RATE_LIMITED_STATUS 429
#define MAX_JAVA_INT INT64_C(2147483647)

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

/** The exact key first, then the first key matching ignoring case, as the JavaScript Headers API reads. */
static const char *header(const cJSON *headers, const char *name) {
    const cJSON *direct = cJSON_GetObjectItemCaseSensitive(headers, name);
    if (cJSON_IsString(direct)) {
        return direct->valuestring;
    }
    const cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, headers) {
        if (cJSON_IsString(entry) && equals_ignoring_case(entry->string, name)) {
            return entry->valuestring;
        }
    }
    return NULL;
}

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
    int64_t seconds = lenient_integer(header(headers, "retry-after"));
    if (seconds > 0) {
        return now + (double)seconds * 1000.0;
    }
    return 0;
}

static cJSON *answer_for(const cJSON *argument, double status, const cJSON *headers, double now) {
    cJSON *result = cJSON_CreateObject();
    if (result == NULL || cJSON_AddBoolToObject(result, "rateLimited", is_rate_limited(status)) == NULL) {
        cJSON_Delete(result);
        return NULL;
    }
    double reset = reset_at(argument, headers, now);
    if (reset > 0 && cJSON_AddNumberToObject(result, "resetAt", reset) == NULL) {
        cJSON_Delete(result);
        return NULL;
    }
    return result;
}

SERVICE_EXPORT int32_t forebay_library_routing_outcome(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len) {
    spisor_call call;
    int32_t status = service_open(in, in_len, &call, out, out_len);
    if (status != SPISOR_STATUS_SUCCESS) {
        return status;
    }
    const cJSON *reply_status = NULL;
    const cJSON *headers = NULL;
    const cJSON *now = NULL;
    if (service_require(&call, "status", service_is_number, "a number", &reply_status, out, out_len, &status) &&
        service_require(&call, "headers", service_is_object, "an object", &headers, out, out_len, &status) &&
        service_require(&call, "now", service_is_number, "a number", &now, out, out_len, &status)) {
        status = service_answer(answer_for(call.argument, reply_status->valuedouble, headers, now->valuedouble), out, out_len);
    }
    spisor_discard_call(&call);
    return status;
}
