#include "anthropic_call.h"

#include "allocation.h"
#include "allocation_failure.h"
#include "spisor/json_allocation.h"

/**
 * @file
 * Fails every allocation of decoding and of encoding each `request-*` and `response-*` vector, one at a time and from
 * that one on, and requires each failure to answer OUT_OF_MEMORY with every block released and the answer empty, or,
 * where the failed allocation was not one the call needed, the answer it gives without one. A failure is never read as
 * a refusal or a malformed call.
 *
 * @implNote The cases a vector does not write are not repeated here; the vectors, among them the golden request and
 * response and the ones holding every block kind, reach every allocation site of both directions.
 */

#define ATTEMPT_LIMIT 100000

/** One failing decode: 1 to go on with the next allocation, 0 once the call ran whole or failed a check. */
static int fail_decode_once(const char *body, const cJSON *expected, const char *label,
                            allocation_failure_mode mode, uint64_t attempt) {
    forebay_anthropic_request_answer answer;
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    start_empty(&answer, sizeof answer);
    int64_t baseline = spisor_memory_live_blocks();
    fail_allocation(mode, attempt);
    spisor_status status = anthropic_decode_text(body, &answer, &refusal);
    int failed = spisor_memory_failed();
    spisor_memory_fail_at(0);
    int answered = empty_unless_answered(status, &answer, sizeof answer) == SPISOR_OUT_OF_MEMORY ||
                   (status == SPISOR_OK && anthropic_request_equals(answer.request, expected));
    anthropic_definition()->release_request(NULL, &answer);
    anthropic_definition()->release_refusal(NULL, &refusal);
    int64_t leaked = spisor_memory_live_blocks() - baseline;
    if (leaked != 0 || !answered) {
        support_failf(label, "failing allocation %llu answered %d, leaving %lld blocks", (unsigned long long)attempt,
                      (int)status, (long long)leaked);
        return 0;
    }
    return failed;
}

/** One failing encode, the IR read before anything is armed. */
static int fail_encode_once(const forebay_ir_request *request, const cJSON *expected, const char *label,
                            allocation_failure_mode mode, uint64_t attempt) {
    forebay_anthropic_body_answer answer;
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    start_empty(&answer, sizeof answer);
    int64_t baseline = spisor_memory_live_blocks();
    fail_allocation(mode, attempt);
    spisor_status status = anthropic_encode_request_of(request, &answer, &refusal);
    int failed = spisor_memory_failed();
    spisor_memory_fail_at(0);
    int answered = empty_unless_answered(status, &answer, sizeof answer) == SPISOR_OUT_OF_MEMORY ||
                   (status == SPISOR_OK && anthropic_body_equals(&answer, expected));
    anthropic_definition()->release_body(NULL, &answer);
    anthropic_definition()->release_refusal(NULL, &refusal);
    int64_t leaked = spisor_memory_live_blocks() - baseline;
    if (leaked != 0 || !answered) {
        support_failf(label, "failing allocation %llu answered %d, leaving %lld blocks", (unsigned long long)attempt,
                      (int)status, (long long)leaked);
        return 0;
    }
    return failed;
}

/** One failing response decode, as fail_decode_once does for a request. */
static int fail_decode_response_once(const char *body, const cJSON *expected, const char *label,
                                     allocation_failure_mode mode, uint64_t attempt) {
    forebay_anthropic_response_answer answer;
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    start_empty(&answer, sizeof answer);
    int64_t baseline = spisor_memory_live_blocks();
    fail_allocation(mode, attempt);
    spisor_status status = anthropic_decode_response_text(body, &answer, &refusal);
    int failed = spisor_memory_failed();
    spisor_memory_fail_at(0);
    int answered = empty_unless_answered(status, &answer, sizeof answer) == SPISOR_OUT_OF_MEMORY ||
                   (status == SPISOR_OK && anthropic_response_equals(answer.response, expected));
    anthropic_definition()->release_response(NULL, &answer);
    anthropic_definition()->release_refusal(NULL, &refusal);
    int64_t leaked = spisor_memory_live_blocks() - baseline;
    if (leaked != 0 || !answered) {
        support_failf(label, "failing allocation %llu answered %d, leaving %lld blocks", (unsigned long long)attempt,
                      (int)status, (long long)leaked);
        return 0;
    }
    return failed;
}

/** One failing response encode, the IR read before anything is armed. */
static int fail_encode_response_once(const forebay_ir_response *response, const cJSON *expected, const char *label,
                                     allocation_failure_mode mode, uint64_t attempt) {
    forebay_anthropic_body_answer answer;
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    start_empty(&answer, sizeof answer);
    int64_t baseline = spisor_memory_live_blocks();
    fail_allocation(mode, attempt);
    spisor_status status = anthropic_encode_response_of(response, &answer, &refusal);
    int failed = spisor_memory_failed();
    spisor_memory_fail_at(0);
    int answered = empty_unless_answered(status, &answer, sizeof answer) == SPISOR_OUT_OF_MEMORY ||
                   (status == SPISOR_OK && anthropic_body_equals(&answer, expected));
    anthropic_definition()->release_body(NULL, &answer);
    anthropic_definition()->release_refusal(NULL, &refusal);
    int64_t leaked = spisor_memory_live_blocks() - baseline;
    if (leaked != 0 || !answered) {
        support_failf(label, "failing allocation %llu answered %d, leaving %lld blocks", (unsigned long long)attempt,
                      (int)status, (long long)leaked);
        return 0;
    }
    return failed;
}

static void every_failure_of_response_decode(const char *path, const cJSON *input, const cJSON *output,
                                             allocation_failure_mode mode) {
    char label[1100];
    snprintf(label, sizeof label, "%s%s", path, allocation_failure_mode_name(mode));
    char *body = cJSON_PrintUnformatted(input);
    uint64_t attempt = 1;
    while (attempt < ATTEMPT_LIMIT && fail_decode_response_once(body, output, label, mode, attempt)) {
        attempt++;
    }
    cJSON_free(body);
    if (attempt == 1) {
        support_fail(label, "no allocation was ever failed, so nothing was checked");
    }
}

static void every_failure_of_response_encode(const char *path, const cJSON *input, const cJSON *output,
                                             allocation_failure_mode mode) {
    char label[1100];
    snprintf(label, sizeof label, "%s%s", path, allocation_failure_mode_name(mode));
    ir_json_arena arena = {0};
    forebay_ir_response response;
    if (!anthropic_read_response_ir(input, &arena, &response)) {
        support_fail(label, "the IR is not one the test codec reads");
    } else {
        uint64_t attempt = 1;
        while (attempt < ATTEMPT_LIMIT && fail_encode_response_once(&response, output, label, mode, attempt)) {
            attempt++;
        }
        if (attempt == 1) {
            support_fail(label, "no allocation was ever failed, so nothing was checked");
        }
    }
    ir_json_arena_release(&arena);
}

static void every_failure_of_decode(const char *path, const cJSON *input, const cJSON *output,
                                    allocation_failure_mode mode) {
    char label[1100];
    snprintf(label, sizeof label, "%s%s", path, allocation_failure_mode_name(mode));
    char *body = cJSON_PrintUnformatted(input);
    uint64_t attempt = 1;
    while (attempt < ATTEMPT_LIMIT && fail_decode_once(body, output, label, mode, attempt)) {
        attempt++;
    }
    cJSON_free(body);
    if (attempt == 1) {
        support_fail(label, "no allocation was ever failed, so nothing was checked");
    }
}

static void every_failure_of_encode(const char *path, const cJSON *input, const cJSON *output,
                                    allocation_failure_mode mode) {
    char label[1100];
    snprintf(label, sizeof label, "%s%s", path, allocation_failure_mode_name(mode));
    ir_json_arena arena = {0};
    forebay_ir_request request;
    if (!anthropic_read_ir(input, &arena, &request)) {
        support_fail(label, "the IR is not one the test codec reads");
    } else {
        uint64_t attempt = 1;
        while (attempt < ATTEMPT_LIMIT && fail_encode_once(&request, output, label, mode, attempt)) {
            attempt++;
        }
        if (attempt == 1) {
            support_fail(label, "no allocation was ever failed, so nothing was checked");
        }
    }
    ir_json_arena_release(&arena);
}

static void every_failure_of_vector(const char *path, allocation_failure_mode mode) {
    cJSON *vector = spisor_vector_read(path);
    const cJSON *input = cJSON_GetObjectItemCaseSensitive(vector, "input");
    const cJSON *output = cJSON_GetObjectItemCaseSensitive(vector, "output");
    if (input == NULL || output == NULL) {
        support_fail(path, "not a vector with an input and an output");
    } else if (anthropic_is_direction(vector, "decodeRequest")) {
        every_failure_of_decode(path, input, output, mode);
    } else if (anthropic_is_direction(vector, "encodeRequest")) {
        every_failure_of_encode(path, input, output, mode);
    } else if (anthropic_is_direction(vector, "decodeResponse")) {
        every_failure_of_response_decode(path, input, output, mode);
    } else if (anthropic_is_direction(vector, "encodeResponse")) {
        every_failure_of_response_encode(path, input, output, mode);
    }
    cJSON_Delete(vector);
}

static void every_failure_of_the_vectors(allocation_failure_mode mode) {
    char *list = support_read_text(ANTHROPIC_VECTOR_LIST);
    if (list == NULL) {
        support_fail("vector list", ANTHROPIC_VECTOR_LIST);
        return;
    }
    for (char *line = strtok(list, "\r\n"); line != NULL; line = strtok(NULL, "\r\n")) {
        if (strstr(line, "request-") != NULL || strstr(line, "response-") != NULL) {
            every_failure_of_vector(line, mode);
        }
    }
    free(list);
}

int main(void) {
    spisor_json_use_library_allocator();
    in_every_failure_mode(every_failure_of_the_vectors);
    return support_summary("anthropic_out_of_memory_test");
}
