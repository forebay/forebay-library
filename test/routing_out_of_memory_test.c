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
 * refusal or a malformed call. A catalog vector fails each of its calls in turn, the rest of the run unarmed.
 */

static int answers_as(const routing_call *call, const routing_answers *answers, const cJSON *expected) {
    cJSON *result = routing_result_of(call, answers);
    int same = cJSON_Compare(result, expected, 1);
    cJSON_Delete(result);
    return same;
}

/** Whether every answer and the refusal are as routing_empty_answers left them, whatever the status. */
static int answers_left_empty(const routing_answers *answers) {
    routing_answers empty;
    routing_empty_answers(&empty);
    empty.status = answers->status;
    return memcmp(&empty, answers, sizeof empty) == 0;
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
    int answered = answers.status == SPISOR_OUT_OF_MEMORY ? answers_left_empty(&answers)
                                                          : answers_as(call, &answers, expected);
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
        cJSON_GetObjectItemCaseSensitive(vector, "status") != NULL ||
        routing_operation_named(routing_text_of(vector, "function")) == ROUTING_CATALOG) {
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

/** Which call of a catalog run fails, and how: `target` the event, SIZE_MAX for the open. */
typedef struct catalog_failure {
    size_t target;
    allocation_failure_mode mode;
    uint64_t attempt;
    int armed;
    int failed;
} catalog_failure;

static void arm_target(void *data, size_t event) {
    catalog_failure *failure = data;
    if (event == failure->target) {
        fail_allocation(failure->mode, failure->attempt);
        failure->armed = 1;
    }
}

static void disarm_target(void *data) {
    catalog_failure *failure = data;
    if (failure->armed) {
        failure->failed = spisor_memory_failed();
        spisor_memory_fail_at(0);
        failure->armed = 0;
    }
}

/**
 * One catalog run whose `target` call fails allocation `attempt`: 1 to go on with the next allocation, 0 once the call
 * ran whole or the run failed a check. The run makes a failed call again unarmed, so its answers must be the vector's
 * whole result, which they are only if the failed call left the catalog as it was.
 */
static int fail_catalog_once(routing_call *call, const cJSON *expected, const char *label, catalog_failure *failure) {
    routing_catalog_guard guard = {arm_target, disarm_target, failure};
    routing_answers answers;
    int64_t baseline = spisor_memory_live_blocks();
    failure->failed = 0;
    call->catalog_guard = &guard;
    routing_make_call(call, &answers);
    call->catalog_guard = NULL;
    int answered = answers_as(call, &answers, expected);
    routing_release_answers(&answers);
    int64_t leaked = spisor_memory_live_blocks() - baseline;
    if (leaked != 0 || !answered) {
        support_failf(label, "failing allocation %llu of call %lld answered wrongly or left %lld blocks",
                      (unsigned long long)failure->attempt, (long long)failure->target, (long long)leaked);
        return 0;
    }
    return failure->failed;
}

static void every_failure_of_catalog(const char *path, allocation_failure_mode mode) {
    cJSON *vector = spisor_vector_read(path);
    routing_call call;
    char label[1100];
    snprintf(label, sizeof label, "%s%s", path, allocation_failure_mode_name(mode));
    if (vector == NULL || routing_operation_named(routing_text_of(vector, "function")) != ROUTING_CATALOG) {
        cJSON_Delete(vector);
        return;
    }
    const cJSON *expected = cJSON_GetObjectItemCaseSensitive(vector, "result");
    const cJSON *argument = cJSON_GetObjectItemCaseSensitive(vector, "argument");
    if (!routing_read_call("catalog", argument, &call)) {
        support_fail(label, "not a vector the definition's structs can hold");
    } else {
        size_t events = (size_t)cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(argument, "events"));
        for (size_t target = 0; target <= events; target++) {
            catalog_failure failure = {target == 0 ? SIZE_MAX : target - 1, mode, 1, 0, 0};
            while (failure.attempt < 100000 && fail_catalog_once(&call, expected, label, &failure)) {
                failure.attempt++;
            }
        }
        routing_release_call(&call);
    }
    routing_blocks_release();
    cJSON_Delete(vector);
}

static const forebay_routing_profile tiered = {sizeof(forebay_routing_profile), "source", NULL, 0, NULL, 0,
                                               "^([a-z]+)-\\d", NULL, NULL, 0, 0};

/** Reports `event` with no memory to spare, then again at `retried` with memory back, answering the second status. */
static spisor_status refused_then_reported_again(forebay_catalog_event_argument *event, double retried,
                                                 forebay_catalog_event_answer *answer, const char *label) {
    const forebay_routing_definition *routing = routing_definition();
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    start_empty(answer, sizeof *answer);
    fail_allocation(FAIL_EVERY_LATER_ALLOCATION, 1);
    spisor_status refused = routing->catalog_event(NULL, event, answer, &refusal);
    spisor_memory_fail_at(0);
    if (empty_unless_answered(refused, answer, sizeof *answer) != SPISOR_OUT_OF_MEMORY) {
        support_failf(label, "with no memory answered %d", (int)refused);
    }
    routing->release_catalog_event_answer(NULL, answer);
    routing->release_refusal(NULL, &refusal);
    start_empty(answer, sizeof *answer);
    event->now = retried;
    spisor_status status = routing->catalog_event(NULL, event, answer, &refusal);
    routing->release_refusal(NULL, &refusal);
    return status;
}

/**
 * A refused timer leaves the catalog as it was, so the host's report of it again with a fresh `now` closes the round
 * and releases its waiters, or re-sets the period, as the first report would have.
 */
static void a_refused_timer_reported_again_does_what_it_would_have(void) {
    const forebay_routing_definition *routing = routing_definition();
    spisor_value settings = {.kind = SPISOR_VALUE_RECORD};
    const char *const offered[] = {"a"};
    forebay_catalog_open_argument open = SPISOR_INIT(forebay_catalog_open_argument);
    forebay_catalog_open_answer opened = SPISOR_INIT(forebay_catalog_open_answer);
    forebay_catalog_event_argument event = SPISOR_INIT(forebay_catalog_event_argument);
    forebay_catalog_event_answer answer;
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    int64_t baseline = spisor_memory_live_blocks();
    open.profile = &tiered;
    open.settings = &settings;
    if (routing->catalog_open(NULL, &open, &opened, &refusal) != SPISOR_OK) {
        support_fail("a refused timer", "the catalog did not open");
        routing->release_catalog_open_answer(NULL, &opened);
        routing->release_refusal(NULL, &refusal);
        return;
    }
    event.catalog = opened.catalog;
    event.kind = FOREBAY_CATALOG_READ;
    event.implementations = offered;
    event.implementation_count = 1;
    start_empty(&answer, sizeof answer);
    if (routing->catalog_event(NULL, &event, &answer, &refusal) != SPISOR_OK || answer.wait_round != 1) {
        support_fail("a refused close", "the read did not wait on round 1");
    }
    routing->release_catalog_event_answer(NULL, &answer);
    event.kind = FOREBAY_CATALOG_TIMER;
    event.timer = FOREBAY_CATALOG_ROUND_CLOSE;
    event.round = 1;
    event.now = 2000;
    spisor_status status = refused_then_reported_again(&event, 2001, &answer, "a refused close");
    if (status != SPISOR_OK || answer.released_count != 1 || answer.released[0] != 1) {
        support_failf("a refused close reported again", "answered %d releasing %zu rounds", (int)status,
                      answer.released_count);
    }
    routing->release_catalog_event_answer(NULL, &answer);
    event.timer = FOREBAY_CATALOG_PERIOD;
    event.round = 0;
    event.now = 3600000;
    status = refused_then_reported_again(&event, 3600001, &answer, "a refused period");
    if (status != SPISOR_OK || answer.ask_round != 2 || answer.timer_count != 2 ||
        answer.timers[1].kind != FOREBAY_CATALOG_PERIOD || answer.timers[1].due != 7200001) {
        support_failf("a refused period reported again", "answered %d asking round %llu", (int)status,
                      (unsigned long long)answer.ask_round);
    }
    routing->release_catalog_event_answer(NULL, &answer);
    routing->release_catalog(NULL, opened.catalog);
    routing->release_catalog_open_answer(NULL, &opened);
    routing->release_refusal(NULL, &refusal);
    if (spisor_memory_live_blocks() != baseline) {
        support_fail("a refused timer", "left blocks behind");
    }
}

static const forebay_assignment chain[] = {{"alpha", "alpha-large", "Alpha Large", 0},
                                           {"beta", "beta-large", "Beta Large", 0}};
static const forebay_slot slots[] = {{"large", chain, 2}};
static const char *const running[] = {"alpha", "beta"};
static const forebay_routing_profile profile = {sizeof(forebay_routing_profile), NULL, NULL, 0, NULL, 0, NULL, NULL,
                                                NULL, 0, 0};

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
    forebay_step_answer answer;
    start_empty(&answer, sizeof answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    int64_t baseline = spisor_memory_live_blocks();
    fail_allocation(mode, attempt);
    spisor_status status = routing->step(NULL, argument, &answer, &refusal);
    int failed = spisor_memory_failed();
    spisor_memory_fail_at(0);
    int answered = empty_unless_answered(status, &answer, sizeof answer) == SPISOR_OUT_OF_MEMORY ||
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

static void every_failure_of_a_report(const forebay_walk *walk, uint32_t kind, const char *message,
                                      uint32_t whole_kind, const char *name, allocation_failure_mode mode) {
    forebay_report report = SPISOR_INIT(forebay_report);
    forebay_step_argument later = SPISOR_INIT(forebay_step_argument);
    report.kind = kind;
    report.after_bytes = kind == FOREBAY_REPORT_STREAM_FAILED ? 1u : 0u;
    report.message = message;
    later.providers = running;
    later.provider_count = 2;
    later.walk = walk;
    later.report = &report;
    every_failure_of_step(&later, whole_kind, name, mode);
}

static void every_failure_of_the_steps(allocation_failure_mode mode) {
    const forebay_routing_definition *routing = routing_definition();
    forebay_step_argument first = first_step();
    forebay_step_argument unnamed = first_step();
    forebay_step_answer answer = SPISOR_INIT(forebay_step_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    unnamed.model = NULL;
    every_failure_of_step(&first, FOREBAY_STEP_CALL_PROVIDER, "first step", mode);
    every_failure_of_step(&unnamed, FOREBAY_STEP_REFRESH_MODELS, "first step naming no model", mode);
    if (routing->step(NULL, &first, &answer, &refusal) != SPISOR_OK || answer.walk == NULL) {
        support_fail("later step", "the first step answered no walk");
        return;
    }
    every_failure_of_a_report(answer.walk, FOREBAY_REPORT_THROWN, "boom", FOREBAY_STEP_CALL_PROVIDER, "later step",
                              mode);
    every_failure_of_a_report(answer.walk, FOREBAY_REPORT_THROWN, NULL, FOREBAY_STEP_CALL_PROVIDER,
                              "later step, a throw with no message", mode);
    every_failure_of_a_report(answer.walk, FOREBAY_REPORT_STREAM_FAILED, NULL, FOREBAY_STEP_END_STREAM,
                              "later step, a failed stream with no message", mode);
    routing->release_walk(NULL, answer.walk);
    routing->release_step_answer(NULL, &answer);
}

static const forebay_application application = {sizeof(forebay_application), "BASE_URL", "TOKEN", "placeholder",
                                                 NULL, 0, NULL, NULL, NULL, NULL};

/**
 * A port out of range is MALFORMED before anything is allocated, so memory running out cannot answer it.
 *
 * @implNote With no refusal to fill, since filling one is itself an allocation.
 */
static void a_port_out_of_range_is_malformed_without_memory(uint32_t port) {
    const forebay_routing_definition *routing = routing_definition();
    forebay_environment_argument argument = SPISOR_INIT(forebay_environment_argument);
    forebay_environment_answer answer = SPISOR_INIT(forebay_environment_answer);
    char label[64];
    snprintf(label, sizeof label, "environment on port %u", (unsigned)port);
    argument.slots = slots;
    argument.slot_count = 1;
    argument.port = port;
    argument.user_home = "/home";
    argument.application = &application;
    fail_allocation(FAIL_EVERY_LATER_ALLOCATION, 1);
    spisor_status status = routing->environment(NULL, &argument, &answer, NULL);
    spisor_memory_fail_at(0);
    if (status != SPISOR_MALFORMED) {
        support_failf(label, "answered %d with no memory to spare", (int)status);
    }
    routing->release_environment_answer(NULL, &answer);
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
            every_failure_of_catalog(line, FAIL_ONE_ALLOCATION);
            every_failure_of_catalog(line, FAIL_EVERY_LATER_ALLOCATION);
            count++;
        }
    }
    free(list);
    every_failure_of_the_steps(FAIL_ONE_ALLOCATION);
    every_failure_of_the_steps(FAIL_EVERY_LATER_ALLOCATION);
    a_refused_timer_reported_again_does_what_it_would_have();
    a_port_out_of_range_is_malformed_without_memory(0);
    a_port_out_of_range_is_malformed_without_memory(70000);
    printf("routing_out_of_memory_test: %d vectors and the steps, every allocation failed singly and for good\n",
           count);
    return support_summary("routing_out_of_memory_test");
}
