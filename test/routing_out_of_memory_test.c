#include "support.h"

#include "allocation.h"
#include "allocation_failure.h"
#include "spisor/json_allocation.h"
#include "routing_call.h"

/**
 * @file
 * Fails every allocation of every routing vector and of a first and a later step, one at a time and from that one on,
 * and requires each failure to answer OUT_OF_MEMORY with every block released and the answer empty, or, where the
 * failed allocation was not one the call needed, the answer it gives without one. A failure is never read as a
 * refusal or a malformed call.
 */

static int answers_as(const routing_call *call, const routing_answers *answers, const cJSON *expected) {
    cJSON *result = routing_result_of(call, answers);
    int same = cJSON_Compare(result, expected, 1);
    cJSON_Delete(result);
    return same;
}

/** One failing call of a vector: 1 to go on with the next allocation, 0 once the call ran whole or failed a check. */
static int fail_vector_once(const routing_call *call, const cJSON *expected, const char *label,
                            allocation_failure_mode mode, uint64_t attempt) {
    routing_answers answers;
    int64_t baseline = spisor_memory_live_blocks();
    fail_allocation(mode, attempt);
    routing_make_call(call, &answers);
    int failed = spisor_memory_failed();
    spisor_memory_fail_at(0);
    int answered = answers.status == SPISOR_OUT_OF_MEMORY || answers_as(call, &answers, expected);
    routing_release_answers(&answers);
    int64_t leaked = spisor_memory_live_blocks() - baseline;
    if (leaked != 0 || !answered) {
        support_failf(label, "failing allocation %llu answered %d, %s, leaving %lld blocks",
                      (unsigned long long)attempt, (int)answers.status, answered ? "as required" : "wrongly",
                      (long long)leaked);
        return 0;
    }
    return failed;
}

static void every_failure_of_vector(const char *path, allocation_failure_mode mode) {
    cJSON *vector = spisor_vector_read(path);
    routing_call call;
    char label[1100];
    snprintf(label, sizeof label, "%s%s", path, allocation_failure_mode_name(mode));
    if (vector == NULL || cJSON_GetObjectItemCaseSensitive(vector, "files") != NULL ||
        cJSON_GetObjectItemCaseSensitive(vector, "status") != NULL) {
        cJSON_Delete(vector);
        return;
    }
    const cJSON *expected = cJSON_GetObjectItemCaseSensitive(vector, "result");
    if (!routing_read_call(routing_text_of(vector, "function"), cJSON_GetObjectItemCaseSensitive(vector, "argument"),
                           &call)) {
        support_fail(label, "not a vector the definition's structs can hold");
    } else {
        uint64_t attempt = 1;
        while (attempt < 100000 && fail_vector_once(&call, expected, label, mode, attempt)) {
            attempt++;
        }
        routing_release_call(&call);
    }
    routing_blocks_release();
    cJSON_Delete(vector);
}

static const forebay_assignment chain[] = {{"alpha", "alpha-large", "Alpha Large", 0},
                                           {"beta", "beta-large", "Beta Large", 0}};
static const forebay_slot slots[] = {{"large", chain, 2}};
static const char *const running[] = {"alpha", "beta"};
static const forebay_routing_profile profile = {sizeof(forebay_routing_profile), NULL, NULL, 0, NULL, 0, NULL, NULL,
                                                NULL};

static forebay_step_argument first_step(void) {
    forebay_step_argument argument = SPISOR_INIT(forebay_step_argument);
    argument.providers = running;
    argument.provider_count = 2;
    argument.profile = &profile;
    argument.slots = slots;
    argument.slot_count = 1;
    argument.model = "acme-large-1";
    argument.login_command = "host-cli action session";
    argument.settings_file = "/home/settings.json";
    return argument;
}

/** One failing step: 1 to go on with the next allocation, 0 once it ran whole or failed a check. */
static int fail_step_once(const forebay_step_argument *argument, uint32_t whole_kind, const char *label,
                          allocation_failure_mode mode, uint64_t attempt) {
    const forebay_routing_definition *routing = routing_definition();
    forebay_step_answer answer = SPISOR_INIT(forebay_step_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    int64_t baseline = spisor_memory_live_blocks();
    fail_allocation(mode, attempt);
    spisor_status status = routing->step(NULL, argument, &answer, &refusal);
    int failed = spisor_memory_failed();
    spisor_memory_fail_at(0);
    int answered = (status == SPISOR_OUT_OF_MEMORY && answer.storage == NULL && answer.walk == NULL) ||
                   (status == SPISOR_OK && answer.kind == whole_kind);
    routing->release_walk(NULL, answer.walk);
    routing->release_step_answer(NULL, &answer);
    routing->release_refusal(NULL, &refusal);
    int64_t leaked = spisor_memory_live_blocks() - baseline;
    if (leaked != 0 || !answered) {
        support_failf(label, "failing allocation %llu answered %d, leaving %lld blocks", (unsigned long long)attempt,
                      (int)status, (long long)leaked);
        return 0;
    }
    return failed;
}

static void every_failure_of_step(const forebay_step_argument *argument, uint32_t whole_kind, const char *name,
                                  allocation_failure_mode mode) {
    char label[160];
    snprintf(label, sizeof label, "%s%s", name, allocation_failure_mode_name(mode));
    uint64_t attempt = 1;
    while (attempt < 100000 && fail_step_once(argument, whole_kind, label, mode, attempt)) {
        attempt++;
    }
}

static void every_failure_of_both_steps(allocation_failure_mode mode) {
    const forebay_routing_definition *routing = routing_definition();
    forebay_step_argument first = first_step();
    forebay_step_answer answer = SPISOR_INIT(forebay_step_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    forebay_report report = SPISOR_INIT(forebay_report);
    forebay_step_argument later = SPISOR_INIT(forebay_step_argument);
    every_failure_of_step(&first, FOREBAY_STEP_CALL_PROVIDER, "first step", mode);
    if (routing->step(NULL, &first, &answer, &refusal) != SPISOR_OK || answer.walk == NULL) {
        support_fail("later step", "the first step answered no walk");
        return;
    }
    report.kind = FOREBAY_REPORT_THROWN;
    report.message = "boom";
    later.providers = running;
    later.provider_count = 2;
    later.walk = answer.walk;
    later.report = &report;
    every_failure_of_step(&later, FOREBAY_STEP_CALL_PROVIDER, "later step", mode);
    routing->release_walk(NULL, answer.walk);
    routing->release_step_answer(NULL, &answer);
}

int main(void) {
    spisor_json_use_library_allocator();
    char *list = support_read_text(ROUTING_VECTOR_LIST);
    int count = 0;
    if (list == NULL) {
        support_fail("vector list", ROUTING_VECTOR_LIST);
        return 1;
    }
    for (char *line = strtok(list, "\r\n"); line != NULL; line = strtok(NULL, "\r\n")) {
        if (line[0] != '\0') {
            every_failure_of_vector(line, FAIL_ONE_ALLOCATION);
            every_failure_of_vector(line, FAIL_EVERY_LATER_ALLOCATION);
            count++;
        }
    }
    free(list);
    every_failure_of_both_steps(FAIL_ONE_ALLOCATION);
    every_failure_of_both_steps(FAIL_EVERY_LATER_ALLOCATION);
    printf("routing_out_of_memory_test: %d vectors and two steps, every allocation failed singly and for good\n",
           count);
    return support_summary("routing_out_of_memory_test");
}
