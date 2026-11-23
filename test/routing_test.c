#include "support.h"

#include "spisor/json_allocation.h"
#include "routing_call.h"
#include "spisor/allocation.h"

/**
 * @file
 * Every routing vector called through the definition, its answer compared with the result the vector writes; a vector
 * with `"status": 1` is a call the definition refuses as MALFORMED. Then the calls no vector writes: structs absent,
 * short or not empty, a required text NULL, an array NULL beside a count, and enum values the header does not name.
 */

/**
 * Lays out the vector's files in a fresh directory and points the argument at it: its userHome, or with
 * `filesUnder` the environment variable that names it, the userHome then left as the vector gives it.
 */
static void lay_out_files(cJSON *argument, const cJSON *files, const cJSON *under) {
    char *directory = support_scratch();
    const cJSON *file = NULL;
    cJSON_ArrayForEach(file, files) {
        char *path = support_join(directory, file->string);
        support_write(path, file->valuestring);
        spisor_memory_release(path);
    }
    cJSON *target = cJSON_IsString(under) ? cJSON_GetObjectItemCaseSensitive(argument, "environment") : argument;
    const char *member = cJSON_IsString(under) ? under->valuestring : "userHome";
    cJSON_DeleteItemFromObjectCaseSensitive(target, member);
    cJSON_AddStringToObject(target, member, directory);
    spisor_memory_release(directory);
}

static void expect_answer(const cJSON *vector, const routing_call *call, const routing_answers *answers,
                          const char *path) {
    const cJSON *result = cJSON_GetObjectItemCaseSensitive(vector, "result");
    if (cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(vector, "status"))) {
        if (answers->status != SPISOR_MALFORMED || answers->refusal.detail == NULL || answers->refusal.fix == NULL) {
            support_failf(path, "expected MALFORMED with a refusal, got %d", (int)answers->status);
        }
        return;
    }
    cJSON *actual = routing_result_of(call, answers);
    if (!cJSON_Compare(actual, result, 1)) {
        char *wanted = cJSON_PrintUnformatted(result);
        char *got = cJSON_PrintUnformatted(actual);
        support_failf(path, "status %d\n  expected %s\n  actual   %s", (int)answers->status, wanted, got);
        cJSON_free(wanted);
        cJSON_free(got);
    }
    cJSON_Delete(actual);
}

static void run_vector(const char *path) {
    cJSON *vector = spisor_vector_read(path);
    cJSON *argument = cJSON_GetObjectItemCaseSensitive(vector, "argument");
    const cJSON *files = cJSON_GetObjectItemCaseSensitive(vector, "files");
    routing_call call;
    routing_answers answers;
    if (files != NULL) {
        lay_out_files(argument, files, cJSON_GetObjectItemCaseSensitive(vector, "filesUnder"));
    }
    if (vector == NULL || cJSON_GetObjectItemCaseSensitive(vector, "result") == NULL ||
        !routing_read_call(routing_text_of(vector, "function"), argument, &call)) {
        support_fail(path, "not a vector the definition's structs can hold");
    } else {
        routing_make_call(&call, &answers);
        expect_answer(vector, &call, &answers, path);
        routing_release_answers(&answers);
        routing_release_call(&call);
    }
    routing_blocks_release();
    cJSON_Delete(vector);
}

static int run_every_vector(void) {
    char *list = support_read_text(ROUTING_VECTOR_LIST);
    if (list == NULL) {
        support_fail("vector list", ROUTING_VECTOR_LIST);
        return 0;
    }
    int count = 0;
    for (char *line = strtok(list, "\r\n"); line != NULL; line = strtok(NULL, "\r\n")) {
        if (line[0] != '\0') {
            run_vector(line);
            count++;
        }
    }
    free(list);
    return count;
}

static void expect_malformed(spisor_status status, spisor_refusal *refusal, const char *label) {
    if (status != SPISOR_MALFORMED || refusal->detail == NULL || refusal->fix == NULL) {
        support_failf(label, "expected MALFORMED with a refusal, got %d", (int)status);
    }
    if (refusal->fix != NULL && (strstr(refusal->fix, "JSON") != NULL || strstr(refusal->fix, "send ") != NULL)) {
        support_failf(label, "the fix speaks JSON, not the struct: %s", refusal->fix);
    }
    routing_definition()->release_refusal(NULL, refusal);
}

static const forebay_routing_profile tier_profile = {
    sizeof(forebay_routing_profile), "p", NULL, 0, NULL, 0, "(opus)", NULL, NULL, 0, 0};

static void refuses_malformed_tiers(void) {
    const forebay_routing_definition *routing = routing_definition();
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    forebay_tiers_argument argument = SPISOR_INIT(forebay_tiers_argument);
    forebay_tiers_answer answer = SPISOR_INIT(forebay_tiers_answer);
    forebay_routing_profile no_regex = tier_profile;
    expect_malformed(routing->tiers(NULL, NULL, &answer, &refusal), &refusal, "tiers without an argument");
    expect_malformed(routing->tiers(NULL, &argument, &answer, &refusal), &refusal, "tiers without a profile");
    no_regex.tier_regex = NULL;
    argument.profile = &no_regex;
    expect_malformed(routing->tiers(NULL, &argument, &answer, &refusal), &refusal, "tiers without tier_regex");
    argument.profile = &tier_profile;
    argument.provider_count = 2;
    expect_malformed(routing->tiers(NULL, &argument, &answer, &refusal), &refusal, "tiers, a count at no catalog");
    argument.provider_count = 0;
    answer.count = 3;
    expect_malformed(routing->tiers(NULL, &argument, &answer, &refusal), &refusal,
                     "tiers into an answer whose count is not zero beside no storage");
    answer.count = 0;
    argument.size = 4;
    expect_malformed(routing->tiers(NULL, &argument, &answer, &refusal), &refusal, "tiers, a short argument");
}

static void refuses_malformed_failures_and_ports(void) {
    const forebay_routing_definition *routing = routing_definition();
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    forebay_failure_argument failure = SPISOR_INIT(forebay_failure_argument);
    forebay_failure_answer failed = SPISOR_INIT(forebay_failure_answer);
    forebay_port_argument port = SPISOR_INIT(forebay_port_argument);
    forebay_port_answer ported = SPISOR_INIT(forebay_port_answer);
    failure.reason = 9;
    failure.detail = "gone";
    expect_malformed(routing->failure(NULL, &failure, &failed, &refusal), &refusal, "failure of an unknown reason");
    failure.reason = FOREBAY_UPSTREAM_UNREACHABLE;
    expect_malformed(routing->failure(NULL, &failure, &failed, &refusal), &refusal, "unreachable without upstream");
    port.settings_file = "/s.json";
    expect_malformed(routing->port(NULL, &port, &ported, &refusal), &refusal, "port without a value");
}

static void refuses_malformed_steps(void) {
    const forebay_routing_definition *routing = routing_definition();
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    forebay_step_argument step = SPISOR_INIT(forebay_step_argument);
    forebay_step_answer stepped = SPISOR_INIT(forebay_step_answer);
    forebay_arrival_argument arrival = SPISOR_INIT(forebay_arrival_argument);
    forebay_arrival_answer arrived = SPISOR_INIT(forebay_arrival_answer);
    forebay_pair unnamed = {NULL, "x"};
    step.profile = &tier_profile;
    step.login_command = "forebay";
    expect_malformed(routing->step(NULL, &step, &stepped, &refusal), &refusal, "a first step without settings_file");
    step.settings_file = "/s.json";
    step.provider_count = 1;
    expect_malformed(routing->step(NULL, &step, &stepped, &refusal), &refusal, "a step, a count at no providers");
    arrival.path = "/";
    arrival.headers = &unnamed;
    arrival.header_count = 1;
    arrival.port = 80;
    expect_malformed(routing->arrival(NULL, &arrival, &arrived, &refusal), &refusal, "an arrival, an unnamed header");
}

static void refuses_malformed_model_entries(void) {
    const forebay_routing_definition *routing = routing_definition();
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    forebay_model_entries_argument argument = SPISOR_INIT(forebay_model_entries_argument);
    forebay_model_entries_answer answer = SPISOR_INIT(forebay_model_entries_answer);
    const forebay_catalog_model unnamed = {NULL, "x", 0, 0, 0, 0};
    const forebay_catalog_provider provider = {"p", &unnamed, 1, 0, NULL, 0, NULL};
    expect_malformed(routing->model_entries(NULL, NULL, &answer, &refusal), &refusal, "entries without an argument");
    expect_malformed(routing->model_entries(NULL, &argument, &answer, &refusal), &refusal,
                     "entries without a profile");
    argument.profile = &tier_profile;
    argument.catalog_count = 1;
    expect_malformed(routing->model_entries(NULL, &argument, &answer, &refusal), &refusal,
                     "entries, a count at no catalog");
    argument.catalog = &provider;
    expect_malformed(routing->model_entries(NULL, &argument, &answer, &refusal), &refusal,
                     "entries, a model without an id");
    argument.catalog_count = 0;
    answer.entry_count = 2;
    expect_malformed(routing->model_entries(NULL, &argument, &answer, &refusal), &refusal,
                     "entries into an answer whose count is not zero beside no storage");
    answer.entry_count = 0;
    argument.size = 4;
    expect_malformed(routing->model_entries(NULL, &argument, &answer, &refusal), &refusal,
                     "entries, a short argument");
}

static void refuses_malformed_catalogs(void) {
    const forebay_routing_definition *routing = routing_definition();
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    forebay_catalog_argument argument = SPISOR_INIT(forebay_catalog_argument);
    forebay_catalog_answer answer = SPISOR_INIT(forebay_catalog_answer);
    expect_malformed(routing->catalog(NULL, NULL, &answer, &refusal), &refusal, "a catalog without an argument");
    expect_malformed(routing->catalog(NULL, &argument, &answer, &refusal), &refusal, "a catalog of no event");
    argument.event = FOREBAY_CATALOG_DERIVE_FAILED + 1;
    expect_malformed(routing->catalog(NULL, &argument, &answer, &refusal), &refusal, "a catalog of an unnamed event");
    argument.event = FOREBAY_CATALOG_PERIOD;
    argument.now = INFINITY;
    expect_malformed(routing->catalog(NULL, &argument, &answer, &refusal), &refusal, "a catalog at no finite now");
    argument.now = 1;
    argument.asked = 1;
    argument.last_asked = NAN;
    expect_malformed(routing->catalog(NULL, &argument, &answer, &refusal), &refusal,
                     "a catalog asked at no finite time");
    argument.last_asked = 0;
    answer.log_count = 1;
    expect_malformed(routing->catalog(NULL, &argument, &answer, &refusal), &refusal,
                     "a catalog into an answer whose count is not zero beside no storage");
    answer.log_count = 0;
    argument.size = 4;
    expect_malformed(routing->catalog(NULL, &argument, &answer, &refusal), &refusal, "a catalog, a short argument");
    routing->release_catalog_answer(NULL, NULL);
}

/**
 * A caller compiled before `unasked` was appended reports every answer as asked; one whose answer stops short of
 * `drop` cannot be told to drop, so an unasked answer is refused it.
 */
static void an_unasked_answer_needs_the_shapes_that_carry_it(void) {
    const forebay_routing_definition *routing = routing_definition();
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    forebay_catalog_argument argument = SPISOR_INIT(forebay_catalog_argument);
    forebay_catalog_answer answer = SPISOR_INIT(forebay_catalog_answer);
    argument.event = FOREBAY_CATALOG_ANSWERED;
    argument.round_closed = 1;
    argument.unasked = 1;
    argument.size = (uint32_t)(offsetof(forebay_catalog_argument, message) + sizeof(const char *));
    spisor_status status = routing->catalog(NULL, &argument, &answer, &refusal);
    if (status != SPISOR_OK || answer.drop != 0 || answer.rederive != 1) {
        support_failf("an argument short of unasked", "answered %d, drop %u", (int)status, (unsigned)answer.drop);
    }
    routing->release_catalog_answer(NULL, &answer);
    argument.size = (uint32_t)sizeof argument;
    answer.size = (uint32_t)(offsetof(forebay_catalog_answer, storage) + sizeof(void *));
    expect_malformed(routing->catalog(NULL, &argument, &answer, &refusal), &refusal,
                     "an unasked answer into an answer short of drop");
}

/** A caller compiled before the defaults were appended has a profile ending at `unserved_model_pattern`. */
static void a_profile_short_of_its_defaults_fills_no_default(void) {
    const forebay_routing_definition *routing = routing_definition();
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    forebay_routing_profile short_profile = tier_profile;
    const forebay_catalog_model models[] = {{"own", "Own", 1, 5, 0, 0}, {"none", NULL, 0, 0, 0, 0}};
    const forebay_catalog_provider provider = {"p", models, 2, 0, NULL, 0, NULL};
    forebay_model_entries_argument argument = SPISOR_INIT(forebay_model_entries_argument);
    forebay_model_entries_answer answer = SPISOR_INIT(forebay_model_entries_answer);
    short_profile.size = (uint32_t)(offsetof(forebay_routing_profile, unserved_model_pattern) + sizeof(const char *));
    short_profile.default_context = 7;
    short_profile.default_output = 8;
    argument.profile = &short_profile;
    argument.catalog = &provider;
    argument.catalog_count = 1;
    spisor_status status = routing->model_entries(NULL, &argument, &answer, &refusal);
    if (status != SPISOR_OK || answer.entry_count != 2 || answer.entries[0].has_context != 1 ||
        answer.entries[0].context != 5 || answer.entries[0].has_output != 0 || answer.entries[1].has_context != 0 ||
        answer.entries[1].has_output != 0 || strcmp(answer.entries[1].name, "none") != 0) {
        support_failf("a profile short of its defaults", "answered %d with %zu entries", (int)status,
                      answer.entry_count);
    }
    routing->release_model_entries_answer(NULL, &answer);
    routing->release_refusal(NULL, &refusal);
    if (answer.entries != NULL || answer.entry_count != 0 || answer.storage != NULL) {
        support_fail("a released entries answer", "is not empty");
    }
}

/** A caller compiled before the reply kind was appended has a limits answer ending at `refresh_every_milliseconds`. */
static void a_limits_answer_short_of_the_reply_kind_gets_every_limit_but_it(void) {
    const forebay_routing_definition *routing = routing_definition();
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    forebay_limits_answer answer = SPISOR_INIT(forebay_limits_answer);
    answer.size = (uint32_t)(offsetof(forebay_limits_answer, refresh_every_milliseconds) + sizeof(uint64_t));
    answer.too_large_reply_kind = 99;
    spisor_status status = routing->limits(NULL, &answer, &refusal);
    if (status != SPISOR_OK || answer.too_large_status != 413 || answer.too_large_reply_kind != 99) {
        support_failf("a limits answer short of the reply kind", "answered %d, kind %u", (int)status,
                      (unsigned)answer.too_large_reply_kind);
    }
    routing->release_refusal(NULL, &refusal);
}

int main(void) {
    spisor_json_use_library_allocator();
    int vectors = run_every_vector();
    if (vectors == 0) {
        support_fail("vectors", "no routing vector was found");
    }
    refuses_malformed_tiers();
    refuses_malformed_failures_and_ports();
    refuses_malformed_steps();
    refuses_malformed_model_entries();
    refuses_malformed_catalogs();
    an_unasked_answer_needs_the_shapes_that_carry_it();
    a_profile_short_of_its_defaults_fills_no_default();
    a_limits_answer_short_of_the_reply_kind_gets_every_limit_but_it();
    printf("routing_test: %d vectors\n", vectors);
    return support_summary("routing_test");
}
