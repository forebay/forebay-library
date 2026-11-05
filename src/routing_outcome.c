#include "routing_outcome.h"

#include <ctype.h>
#include <stdint.h>
#include <string.h>

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

static int is_rate_limited(double status, const cJSON *headers) {
    const char *marker = header(headers, "x-hub-rate-limited");
    return status == RATE_LIMITED_STATUS || (marker != NULL && strcmp(marker, "1") == 0);
}

/** Epoch milliseconds the response says it is usable again, or 0 when it names none. */
static double reset_at(const cJSON *headers, double now) {
    int64_t milliseconds = lenient_integer(header(headers, "x-hub-retry-after-ms"));
    if (milliseconds > 0) {
        return now + (double)milliseconds;
    }
    int64_t seconds = lenient_integer(header(headers, "retry-after"));
    if (seconds > 0) {
        return now + (double)seconds * 1000.0;
    }
    return 0;
}

static cJSON *answer_for(double status, const cJSON *headers, double now) {
    cJSON *result = cJSON_CreateObject();
    if (result == NULL || cJSON_AddBoolToObject(result, "rateLimited", is_rate_limited(status, headers)) == NULL) {
        cJSON_Delete(result);
        return NULL;
    }
    double reset = reset_at(headers, now);
    if (reset > 0 && cJSON_AddNumberToObject(result, "resetAt", reset) == NULL) {
        cJSON_Delete(result);
        return NULL;
    }
    return result;
}

ROUTING_EXPORT int32_t routing_outcome(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len) {
    spisor_call call;
    int32_t status = routing_open(in, in_len, &call, out, out_len);
    if (status != SPISOR_STATUS_SUCCESS) {
        return status;
    }
    const cJSON *reply_status = NULL;
    const cJSON *headers = NULL;
    const cJSON *now = NULL;
    if (routing_require(&call, "status", routing_is_number, "a number", &reply_status, out, out_len, &status) &&
        routing_require(&call, "headers", routing_is_object, "an object", &headers, out, out_len, &status) &&
        routing_require(&call, "now", routing_is_number, "a number", &now, out, out_len, &status)) {
        status = routing_answer(answer_for(reply_status->valuedouble, headers, now->valuedouble), out, out_len);
    }
    spisor_discard_call(&call);
    return status;
}
