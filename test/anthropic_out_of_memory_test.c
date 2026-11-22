#include "anthropic_call.h"
#include "anthropic_shape_cases.h"

#include "allocation.h"
#include "allocation_failure.h"
#include "spisor/json_allocation.h"

/**
 * @file
 * Fails every allocation of decoding and of encoding each vector, a request, a response or a whole stream fed as the
 * vector lists it, one at a time and from that one on, and requires each failure to answer OUT_OF_MEMORY with every
 * block released and the answer empty, or, where the failed allocation was not one the call needed, the answer it
 * gives without one. A failure is never read as a refusal or a malformed call. A stream feed that answers
 * OUT_OF_MEMORY is fed again, since the header says it leaves its handle as it was, so a single failure must still give
 * the whole stream.
 *
 * The wire shapes, `error_envelope` and `model_listing`, are failed over their cases the same way.
 *
 * @implNote The cases a vector does not write are not repeated here; the vectors, among them the golden request and
 * response and the ones holding every block kind, reach every allocation site of both directions.
 */

#define ATTEMPT_LIMIT 100000

/** One failing call: 1 to go on with the next allocation, 0 once the call ran whole or failed a check. */
static int fail_once(const anthropic_direction *direction, const anthropic_input *input, const cJSON *expected,
                     const char *label, allocation_failure_mode mode, uint64_t attempt) {
    anthropic_answer answer;
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    anthropic_start_answer(direction, &answer);
    int64_t baseline = spisor_memory_live_blocks();
    fail_allocation(mode, attempt);
    spisor_status status = direction->call(input, &answer, &refusal);
    int failed = spisor_memory_failed();
    spisor_memory_fail_at(0);
    int answered = empty_unless_answered(status, &answer, direction->answer_size) == SPISOR_OUT_OF_MEMORY ||
                   (status == SPISOR_OK && direction->matches(&answer, expected));
    direction->release(&answer);
    anthropic_definition()->release_refusal(NULL, &refusal);
    int64_t leaked = spisor_memory_live_blocks() - baseline;
    if (leaked != 0 || !answered) {
        support_failf(label, "failing allocation %llu answered %d, leaving %lld blocks", (unsigned long long)attempt,
                      (int)status, (long long)leaked);
        return 0;
    }
    return failed;
}

/** Fails every allocation of one vector's call, its input read before anything is armed. */
static void every_failure_of(const anthropic_direction *direction, const char *path, const cJSON *input_json,
                             const cJSON *expected, allocation_failure_mode mode) {
    char label[1100];
    snprintf(label, sizeof label, "%s%s", path, allocation_failure_mode_name(mode));
    anthropic_input input;
    if (!direction->read_input(input_json, &input)) {
        support_fail(label, "the input is not one the test reads");
    } else {
        uint64_t attempt = 1;
        while (attempt < ATTEMPT_LIMIT && fail_once(direction, &input, expected, label, mode, attempt)) {
            attempt++;
        }
        if (attempt == 1) {
            support_fail(label, "no allocation was ever failed, so nothing was checked");
        }
    }
    anthropic_input_release(&input);
}

static void every_failure_of_vector(const char *path, allocation_failure_mode mode) {
    cJSON *vector = spisor_vector_read(path);
    const cJSON *input = cJSON_GetObjectItemCaseSensitive(vector, "input");
    const cJSON *output = cJSON_GetObjectItemCaseSensitive(vector, "output");
    const anthropic_direction *direction = anthropic_direction_of(vector);
    if (input == NULL || output == NULL || direction == NULL) {
        support_fail(path, "not a vector with an input, an output and a direction this service has");
    } else if (anthropic_vector_allocates(vector)) {
        every_failure_of(direction, path, input, output, mode);
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
        every_failure_of_vector(line, mode);
    }
    free(list);
}

static void every_failure_of_the_shape_cases(allocation_failure_mode mode) {
    for (size_t index = 0; index < ANTHROPIC_SHAPE_CASE_COUNT; index++) {
        const anthropic_shape_case *shape = &ANTHROPIC_SHAPE_CASES[index];
        int out_of_memory = 0;
        cJSON *input = spisor_parse_json_reporting((const uint8_t *)shape->input, strlen(shape->input), &out_of_memory);
        cJSON *expected =
            spisor_parse_json_reporting((const uint8_t *)shape->expected, strlen(shape->expected), &out_of_memory);
        const anthropic_direction *direction = anthropic_direction_named(shape->direction);
        if (input == NULL || expected == NULL || direction == NULL) {
            support_fail(shape->label, "the case could not be read");
        } else {
            every_failure_of(direction, shape->label, input, expected, mode);
        }
        cJSON_Delete(input);
        cJSON_Delete(expected);
    }
}

static void every_failure_of_everything(allocation_failure_mode mode) {
    every_failure_of_the_vectors(mode);
    every_failure_of_the_shape_cases(mode);
}

int main(void) {
    spisor_json_use_library_allocator();
    in_every_failure_mode(every_failure_of_everything);
    return support_summary("anthropic_out_of_memory_test");
}
