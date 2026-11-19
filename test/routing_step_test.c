#include "support.h"

#include "spisor/json_allocation.h"
#include "routing_blocks.h"

/**
 * @file
 * routing's step through its definition, one scenario per function: each call's structs are read from the JSON a
 * scenario writes, and each answer written back as the step and log JSON it expects, so a scenario reads as the
 * walk it describes. A walk is the library's handle, handed back as it came.
 */

#define REQUESTED "acme-large-1"
#define LOGIN_COMMAND "host-cli action session"
#define SETTINGS_FILE "/home/settings.json"
#define TWO_ENTRIES                                                                                        \
    "{\"large\":[{\"provider\":\"alpha\",\"model\":\"alpha-large\",\"name\":\"Alpha Large\",\"derived\":false}," \
    "{\"provider\":\"beta\",\"model\":\"beta-large\",\"name\":\"Beta Large\",\"derived\":false}]}"
#define BOTH_RUNNING "[\"alpha\",\"beta\"]"

/** What one step answered: the step and the log as JSON, the walk handle, and the status with its refusal. */
typedef struct step_result {
    spisor_status status;
    spisor_refusal refusal;
    cJSON *step;
    cJSON *log;
    forebay_walk *walk;
} step_result;

static const forebay_routing_definition *routing(void) {
    return forebay_library_routing_definition();
}

static void release_walk(forebay_walk *walk) {
    routing()->release_walk(NULL, walk);
}

static cJSON *parsed(const char *text) {
    cJSON *json = cJSON_Parse(text);
    if (json == NULL) {
        support_fail("scenario", text);
    }
    return json;
}

static cJSON *reply_json(const forebay_step_answer *answer) {
    cJSON *reply = cJSON_CreateObject();
    cJSON_AddStringToObject(reply, "kind", routing_reply_kind_name(answer->reply_kind));
    if (answer->reply_kind != FOREBAY_REPLY_RATE_LIMITED) {
        cJSON_AddNumberToObject(reply, "status", answer->status);
    }
    if (answer->reply_kind == FOREBAY_REPLY_PROXY_ERROR || answer->reply_kind == FOREBAY_REPLY_API_ERROR) {
        cJSON_AddStringToObject(reply, "message", answer->message);
    } else {
        cJSON_AddItemToObject(reply, "headers", routing_pairs_json_of(answer->headers, answer->header_count));
    }
    if (answer->has_reset_at) {
        cJSON_AddNumberToObject(reply, "resetAt", answer->reset_at);
    }
    return reply;
}

static cJSON *step_json(const forebay_step_answer *answer) {
    static const char *const kinds[] = {"(none)", "passThrough", "refreshModels", "callProvider",
                                        "reply",  "endStream",   "done"};
    cJSON *step = cJSON_CreateObject();
    cJSON_AddStringToObject(step, "kind", answer->kind <= 6 ? kinds[answer->kind] : "(unknown)");
    if (answer->kind == FOREBAY_STEP_CALL_PROVIDER) {
        cJSON_AddStringToObject(step, "provider", answer->provider);
        cJSON_AddStringToObject(step, "model", answer->model);
    } else if (answer->kind == FOREBAY_STEP_REPLY) {
        cJSON_AddItemToObject(step, "reply", reply_json(answer));
    } else if (answer->kind == FOREBAY_STEP_END_STREAM) {
        cJSON *event = cJSON_CreateObject();
        cJSON_AddStringToObject(event, "event", "error");
        cJSON_AddStringToObject(event, "errorType", answer->error_type);
        cJSON_AddStringToObject(event, "message", answer->message);
        cJSON_AddItemToObject(step, "event", event);
    }
    return step;
}

static step_result step_with(const forebay_step_argument *argument) {
    step_result result = {SPISOR_OK, SPISOR_INIT(spisor_refusal), NULL, NULL, NULL};
    forebay_step_answer answer = SPISOR_INIT(forebay_step_answer);
    result.status = routing()->step(NULL, argument, &answer, &result.refusal);
    if (result.status == SPISOR_OK) {
        result.step = step_json(&answer);
        result.log = routing_log_json_of(answer.log, answer.log_count);
        result.walk = answer.walk;
    }
    routing()->release_step_answer(NULL, &answer);
    routing_blocks_release();
    return result;
}

static step_result first_step(const char *profile, const char *map, const char *providers, const char *request,
                              int refreshed) {
    cJSON *profile_json = parsed(profile);
    cJSON *map_json = parsed(map);
    cJSON *providers_json = parsed(providers);
    cJSON *request_json = parsed(request);
    forebay_step_argument argument = SPISOR_INIT(forebay_step_argument);
    argument.providers = routing_texts_of(providers_json, &argument.provider_count);
    argument.profile = routing_profile_of(profile_json);
    argument.slots = routing_slots_of(map_json, &argument.slot_count);
    argument.model = routing_text_of(request_json, "model");
    argument.pass_through = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(request_json, "passThrough")) ? 1u : 0u;
    argument.refreshed = (uint32_t)refreshed;
    argument.login_command = LOGIN_COMMAND;
    argument.settings_file = SETTINGS_FILE;
    step_result result = step_with(&argument);
    cJSON_Delete(profile_json);
    cJSON_Delete(map_json);
    cJSON_Delete(providers_json);
    cJSON_Delete(request_json);
    return result;
}

static step_result requested_step(const char *map, const char *providers) {
    return first_step("{}", map, providers, "{\"model\":\"" REQUESTED "\"}", 0);
}

static uint32_t report_kind_named(const char *kind) {
    static const char *const kinds[] = {"response", "streamed", "streamFailed", "thrown", "failure"};
    for (uint32_t index = 0; kind != NULL && index < 5; index++) {
        if (strcmp(kinds[index], kind) == 0) {
            return FOREBAY_REPORT_RESPONSE + index;
        }
    }
    return 99;
}

static void read_report(const cJSON *json, forebay_report *report) {
    const cJSON *retry = cJSON_GetObjectItemCaseSensitive(json, "retryAfterMilliseconds");
    report->kind = report_kind_named(routing_text_of(json, "kind"));
    report->after_bytes = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "afterBytes")) ? 1u : 0u;
    report->message = routing_text_of(json, "message");
    report->status = cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(json, "status"))
                         ? cJSON_GetObjectItemCaseSensitive(json, "status")->valuedouble
                         : 0;
    report->has_retry_after = cJSON_IsNumber(retry) ? 1u : 0u;
    report->retry_after_milliseconds = cJSON_IsNumber(retry) ? retry->valuedouble : 0;
    report->now = cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(json, "now"))
                      ? cJSON_GetObjectItemCaseSensitive(json, "now")->valuedouble
                      : 0;
    if (!routing_pairs_of(cJSON_GetObjectItemCaseSensitive(json, "headers"), &report->headers, &report->header_count)) {
        support_fail("scenario", "a report header that is not a string");
    }
}

static step_result reported_step(const forebay_walk *walk, const char *providers, const char *report) {
    cJSON *providers_json = parsed(providers);
    cJSON *report_json = parsed(report);
    forebay_report read = SPISOR_INIT(forebay_report);
    forebay_step_argument argument = SPISOR_INIT(forebay_step_argument);
    read_report(report_json, &read);
    argument.providers = routing_texts_of(providers_json, &argument.provider_count);
    argument.walk = walk;
    argument.report = &read;
    step_result result = step_with(&argument);
    cJSON_Delete(providers_json);
    cJSON_Delete(report_json);
    return result;
}

static void expect_json(const cJSON *actual, const char *expected, const char *name, const char *label) {
    cJSON *wanted = cJSON_Parse(expected);
    if (wanted == NULL || !cJSON_Compare(actual, wanted, 1)) {
        char *got = cJSON_PrintUnformatted(actual);
        support_failf(label, "%s differs\n  expected %s\n  answer   %s", name, expected, got);
        cJSON_free(got);
    }
    cJSON_Delete(wanted);
}

/** Checks the step and, unless `log` is NULL, the log, and that a walk is present exactly when expected. */
static forebay_walk *expect_step(step_result *result, const char *step, const char *log, int expects_walk,
                                 const char *label) {
    if (result->status != SPISOR_OK) {
        support_failf(label, "status %d: %s", (int)result->status,
                      result->refusal.detail == NULL ? "" : result->refusal.detail);
    } else {
        expect_json(result->step, step, "step", label);
        if (log != NULL) {
            expect_json(result->log, log, "log", label);
        }
        if ((result->walk != NULL) != expects_walk) {
            support_failf(label, "expected %s walk", expects_walk ? "a" : "no");
        }
    }
    cJSON_Delete(result->step);
    cJSON_Delete(result->log);
    routing()->release_refusal(NULL, &result->refusal);
    return result->walk;
}

static void expect_refused(step_result *result, const char *label) {
    if (result->status != SPISOR_REFUSED || result->refusal.detail == NULL || result->refusal.fix == NULL) {
        support_failf(label, "expected a refusal, got status %d", (int)result->status);
    }
    cJSON_Delete(result->step);
    cJSON_Delete(result->log);
    release_walk(result->walk);
    routing()->release_refusal(NULL, &result->refusal);
}

static void expect_malformed(step_result *result, const char *label) {
    if (result->status != SPISOR_MALFORMED || result->refusal.detail == NULL || result->refusal.fix == NULL) {
        support_failf(label, "expected MALFORMED, got status %d", (int)result->status);
    }
    cJSON_Delete(result->step);
    cJSON_Delete(result->log);
    release_walk(result->walk);
    routing()->release_refusal(NULL, &result->refusal);
}

static forebay_walk *walk_calling_alpha(const char *label) {
    step_result first = requested_step(TWO_ENTRIES, BOTH_RUNNING);
    return expect_step(&first, "{\"kind\":\"callProvider\",\"provider\":\"alpha\",\"model\":\"alpha-large\"}", "[]",
                       1, label);
}

static void a_stream_failing_after_bytes_ends_with_an_ir_error_event(void) {
    const char *label = "a_stream_failing_after_bytes_ends_with_an_ir_error_event";
    forebay_walk *walk = walk_calling_alpha(label);
    step_result ended = reported_step(walk, BOTH_RUNNING,
                                      "{\"kind\":\"streamFailed\",\"afterBytes\":true,\"message\":\"socket closed\"}");
    release_walk(expect_step(
        &ended,
        "{\"kind\":\"endStream\",\"event\":{\"event\":\"error\",\"errorType\":\"api_error\","
        "\"message\":\"socket closed\"}}",
        "[{\"level\":\"warn\",\"message\":\"a stream failed after its first event: socket closed\"}]", 0, label));
    release_walk(walk);
}

static void a_stream_failing_before_bytes_falls_to_the_next_entry(void) {
    const char *label = "a_stream_failing_before_bytes_falls_to_the_next_entry";
    forebay_walk *walk = walk_calling_alpha(label);
    step_result next = reported_step(walk, BOTH_RUNNING,
                                     "{\"kind\":\"streamFailed\",\"afterBytes\":false,\"message\":\"no first event\"}");
    release_walk(expect_step(&next, "{\"kind\":\"callProvider\",\"provider\":\"beta\",\"model\":\"beta-large\"}",
                             "[{\"level\":\"warn\",\"message\":\"the stream of alpha-large failed on its first event, "
                             "so the next chain entry serves " REQUESTED ": no first event\"}]",
                             1, label));
    release_walk(walk);
}

static void a_fallback_stream_failing_after_bytes_counts_as_served(void) {
    const char *label = "a_fallback_stream_failing_after_bytes_counts_as_served";
    forebay_walk *walk = walk_calling_alpha(label);
    step_result next = reported_step(walk, BOTH_RUNNING, "{\"kind\":\"thrown\",\"message\":\"refused\"}");
    forebay_walk *second =
        expect_step(&next, "{\"kind\":\"callProvider\",\"provider\":\"beta\",\"model\":\"beta-large\"}",
                    "[{\"level\":\"warn\",\"message\":\"alpha failed alpha-large, so the next chain entry "
                    "serves " REQUESTED ": refused\"}]",
                    1, label);
    step_result ended = reported_step(second, BOTH_RUNNING,
                                         "{\"kind\":\"streamFailed\",\"afterBytes\":true,\"message\":\"cut\"}");
    release_walk(expect_step(&ended,
                             "{\"kind\":\"endStream\",\"event\":{\"event\":\"error\",\"errorType\":\"api_error\","
                             "\"message\":\"cut\"}}",
                             "[{\"level\":\"warn\",\"message\":\"a stream failed after its first event: cut\"},"
                             "{\"level\":\"warn\",\"message\":\"beta-large served " REQUESTED
                             " in place of alpha-large\"}]",
                             0, label));
    release_walk(second);
    release_walk(walk);
}

static void no_running_provider_passes_through(void) {
    step_result answer = requested_step(TWO_ENTRIES, "[]");
    release_walk(expect_step(&answer, "{\"kind\":\"passThrough\"}", "[]", 0, "no_running_provider_passes_through"));
}

static void a_decoded_pass_through_passes_through(void) {
    step_result answer = first_step("{}", TWO_ENTRIES, BOTH_RUNNING, "{\"passThrough\":true}", 0);
    release_walk(expect_step(&answer, "{\"kind\":\"passThrough\"}", "[]", 0, "a_decoded_pass_through_passes_through"));
}

static void a_served_first_entry_is_done_without_a_notice(void) {
    const char *label = "a_served_first_entry_is_done_without_a_notice";
    forebay_walk *walk = walk_calling_alpha(label);
    step_result response = reported_step(walk, BOTH_RUNNING, "{\"kind\":\"response\"}");
    release_walk(expect_step(&response, "{\"kind\":\"done\"}", "[]", 0, label));
    step_result streamed = reported_step(walk, BOTH_RUNNING, "{\"kind\":\"streamed\"}");
    release_walk(expect_step(&streamed, "{\"kind\":\"done\"}", "[]", 0, label));
    release_walk(walk);
}

static forebay_walk *walk_after_alpha_throws(const char *label) {
    forebay_walk *walk = walk_calling_alpha(label);
    step_result next = reported_step(walk, BOTH_RUNNING, "{\"kind\":\"thrown\",\"message\":\"boom\"}");
    forebay_walk *second =
        expect_step(&next, "{\"kind\":\"callProvider\",\"provider\":\"beta\",\"model\":\"beta-large\"}",
                    "[{\"level\":\"warn\",\"message\":\"alpha failed alpha-large, so the next chain entry "
                    "serves " REQUESTED ": boom\"}]",
                    1, label);
    release_walk(walk);
    return second;
}

static void a_thrown_entry_falls_through(void) {
    release_walk(walk_after_alpha_throws("a_thrown_entry_falls_through"));
}

static void a_fallback_serving_is_logged(void) {
    const char *label = "a_fallback_serving_is_logged";
    forebay_walk *walk = walk_after_alpha_throws(label);
    step_result served = reported_step(walk, BOTH_RUNNING, "{\"kind\":\"response\"}");
    release_walk(expect_step(&served, "{\"kind\":\"done\"}",
                             "[{\"level\":\"warn\",\"message\":\"beta-large served " REQUESTED
                             " in place of alpha-large\"}]",
                             0, label));
    release_walk(walk);
}

static void the_last_entry_says_no_entry_is_left(void) {
    const char *label = "the_last_entry_says_no_entry_is_left";
    forebay_walk *walk = walk_after_alpha_throws(label);
    step_result exhausted = reported_step(walk, BOTH_RUNNING, "{\"kind\":\"thrown\",\"message\":\"bust\"}");
    release_walk(expect_step(&exhausted,
                             "{\"kind\":\"reply\",\"reply\":{\"kind\":\"proxyError\",\"status\":502,"
                             "\"message\":\"no chain entry could serve the request: bust\"}}",
                             "[{\"level\":\"warn\",\"message\":\"beta failed beta-large, and no chain entry is left to "
                             "serve " REQUESTED ": bust\"}]",
                             0, label));
    release_walk(walk);
}

static void a_plain_failure_is_answered_with_end_to_end_headers(void) {
    const char *label = "a_plain_failure_is_answered_with_end_to_end_headers";
    forebay_walk *walk = walk_calling_alpha(label);
    step_result failed = reported_step(walk, BOTH_RUNNING,
                                          "{\"kind\":\"failure\",\"status\":500,\"headers\":{\"content-type\":"
                                          "\"application/json\",\"Connection\":\"close\",\"Content-Length\":\"12\","
                                          "\"content-encoding\":\"gzip\",\"x-trace\":\"t\"},\"now\":1000}");
    release_walk(expect_step(&failed,
                             "{\"kind\":\"reply\",\"reply\":{\"kind\":\"providerFailure\",\"status\":500,"
                             "\"headers\":{\"content-type\":\"application/json\",\"x-trace\":\"t\"}}}",
                             "[]", 0, label));
    release_walk(walk);
}

static forebay_walk *walk_after_alpha_limited(const char *report, const char *label) {
    forebay_walk *walk = walk_calling_alpha(label);
    step_result next = reported_step(walk, BOTH_RUNNING, report);
    forebay_walk *second =
        expect_step(&next, "{\"kind\":\"callProvider\",\"provider\":\"beta\",\"model\":\"beta-large\"}",
                    "[{\"level\":\"warn\",\"message\":\"alpha-large is rate limited, so the next chain "
                    "entry serves " REQUESTED "\"}]",
                    1, label);
    release_walk(walk);
    return second;
}

static void a_rate_limited_entry_falls_through(void) {
    release_walk(walk_after_alpha_limited("{\"kind\":\"failure\",\"status\":429,\"headers\":{},\"now\":1000}",
                                          "a_rate_limited_entry_falls_through"));
}

static void expect_exhausted_reply(forebay_walk *walk, const char *report, const char *reply, const char *log,
                                   const char *label) {
    step_result exhausted = reported_step(walk, BOTH_RUNNING, report);
    release_walk(expect_step(&exhausted, reply, log, 0, label));
    release_walk(walk);
}

static void exhaustion_after_a_limit_answers_rate_limited(void) {
    const char *label = "exhaustion_after_a_limit_answers_rate_limited";
    forebay_walk *walk = walk_after_alpha_limited(
        "{\"kind\":\"failure\",\"status\":429,\"headers\":{\"retry-after\":\"30\"},\"now\":1000}", label);
    expect_exhausted_reply(walk, "{\"kind\":\"thrown\",\"message\":\"bust\"}",
                           "{\"kind\":\"reply\",\"reply\":{\"kind\":\"rateLimited\",\"headers\":{\"retry-after\":"
                           "\"30\"},\"resetAt\":31000}}",
                           "[{\"level\":\"warn\",\"message\":\"beta failed beta-large, and no chain entry is left to "
                           "serve " REQUESTED ": bust\"}]",
                           label);
}

static void the_latest_reset_and_the_last_headers_are_kept(void) {
    const char *label = "the_latest_reset_and_the_last_headers_are_kept";
    const char *first = "{\"kind\":\"failure\",\"status\":429,\"headers\":{\"retry-after\":\"30\",\"x-first\":\"1\"},"
                        "\"now\":1000}";
    const char *exhausted_log = "[{\"level\":\"warn\",\"message\":\"beta-large is rate limited, and no chain entry is "
                                "left to serve " REQUESTED "\"}]";
    expect_exhausted_reply(walk_after_alpha_limited(first, label),
                           "{\"kind\":\"failure\",\"status\":429,\"headers\":{\"x-second\":\"2\"},\"now\":2000}",
                           "{\"kind\":\"reply\",\"reply\":{\"kind\":\"rateLimited\",\"headers\":{\"x-second\":\"2\"},"
                           "\"resetAt\":31000}}",
                           exhausted_log, label);
    expect_exhausted_reply(walk_after_alpha_limited(first, label),
                           "{\"kind\":\"failure\",\"status\":429,\"headers\":{\"retry-after\":\"5\"},\"now\":2000}",
                           "{\"kind\":\"reply\",\"reply\":{\"kind\":\"rateLimited\",\"headers\":{\"retry-after\":"
                           "\"5\"},\"resetAt\":31000}}",
                           exhausted_log, label);
}

static void milliseconds_win_over_retry_after_in_a_report(void) {
    const char *label = "milliseconds_win_over_retry_after_in_a_report";
    forebay_walk *walk =
        walk_after_alpha_limited("{\"kind\":\"failure\",\"status\":429,\"headers\":{\"retry-after\":\"30\"},"
                                 "\"retryAfterMilliseconds\":500,\"now\":1000}",
                                 label);
    expect_exhausted_reply(walk, "{\"kind\":\"thrown\",\"message\":\"bust\"}",
                           "{\"kind\":\"reply\",\"reply\":{\"kind\":\"rateLimited\",\"headers\":{\"retry-after\":"
                           "\"30\"},\"resetAt\":1500}}",
                           "[{\"level\":\"warn\",\"message\":\"beta failed beta-large, and no chain entry is left to "
                           "serve " REQUESTED ": bust\"}]",
                           label);
}

static void a_missing_provider_is_skipped_without_a_call(void) {
    step_result answer = requested_step(TWO_ENTRIES, "[\"beta\"]");
    release_walk(expect_step(&answer, "{\"kind\":\"callProvider\",\"provider\":\"beta\",\"model\":\"beta-large\"}",
                             "[{\"level\":\"warn\",\"message\":\"no running plugin provides alpha, which the model map "
                             "names for alpha-large\"}]",
                             1, "a_missing_provider_is_skipped_without_a_call"));
}

static void a_chain_of_missing_providers_never_calls_one(void) {
    step_result answer = requested_step(TWO_ENTRIES, "[\"gamma\"]");
    release_walk(expect_step(&answer,
                             "{\"kind\":\"reply\",\"reply\":{\"kind\":\"proxyError\",\"status\":502,"
                             "\"message\":\"no chain entry could serve the request: no running plugin provides beta, "
                             "which the model map names for beta-large\"}}",
                             "[{\"level\":\"warn\",\"message\":\"no running plugin provides alpha, which the model map "
                             "names for alpha-large\"},"
                             "{\"level\":\"warn\",\"message\":\"no running plugin provides beta, which the model map "
                             "names for beta-large\"}]",
                             0, "a_chain_of_missing_providers_never_calls_one"));
}

static void an_empty_chain_asks_for_a_refresh_once(void) {
    const char *label = "an_empty_chain_asks_for_a_refresh_once";
    const char *switched = "[{\"level\":\"info\",\"message\":\"routing " REQUESTED ": model_switched\"}]";
    step_result first = requested_step("{}", BOTH_RUNNING);
    release_walk(expect_step(&first, "{\"kind\":\"refreshModels\"}", switched, 0, label));
    step_result refreshed = first_step("{}", "{}", BOTH_RUNNING, "{\"model\":\"" REQUESTED "\"}", 1);
    release_walk(expect_step(&refreshed,
                             "{\"kind\":\"reply\",\"reply\":{\"kind\":\"apiError\",\"status\":503,"
                             "\"message\":\"no running provider offers a model for " REQUESTED "; log a provider in "
                             "with host-cli action session alpha login or host-cli action session beta login, or set "
                             "routing.modelMap in /home/settings.json\"}}",
                             switched, 0, label));
}

static void resolve_notices_are_info_lines(void) {
    step_result answer = requested_step(
        "{\"default\":[{\"provider\":\"alpha\",\"model\":\"alpha-small\",\"name\":\"Alpha Small\",\"derived\":true}]}",
        BOTH_RUNNING);
    release_walk(expect_step(&answer, "{\"kind\":\"callProvider\",\"provider\":\"alpha\",\"model\":\"alpha-small\"}",
                             "[{\"level\":\"info\",\"message\":\"routing " REQUESTED ": model_switched\"},"
                             "{\"level\":\"info\",\"message\":\"routing " REQUESTED ": healed\"}]",
                             1, "resolve_notices_are_info_lines"));
}

static void a_refused_resolve_is_refused(void) {
    step_result answer = first_step("{\"nativeModelPattern\":\"(a|b)\"}", TWO_ENTRIES, BOTH_RUNNING,
                                       "{\"model\":\"" REQUESTED "\"}", 0);
    expect_refused(&answer, "a_refused_resolve_is_refused");
}

static void refuses_a_malformed_call(void) {
    forebay_walk *walk = walk_calling_alpha("refuses_a_malformed_call");
    step_result no_message = reported_step(walk, BOTH_RUNNING, "{\"kind\":\"thrown\"}");
    expect_malformed(&no_message, "step with a thrown report lacking its message");
    step_result unknown = reported_step(walk, BOTH_RUNNING, "{\"kind\":\"vanished\"}");
    expect_malformed(&unknown, "step with a report kind the header does not name");
    release_walk(walk);
}

static void the_same_walk_serves_two_reports(void) {
    const char *label = "the_same_walk_serves_two_reports";
    forebay_walk *walk = walk_calling_alpha(label);
    step_result first = reported_step(walk, BOTH_RUNNING, "{\"kind\":\"thrown\",\"message\":\"one\"}");
    step_result second = reported_step(walk, BOTH_RUNNING, "{\"kind\":\"thrown\",\"message\":\"one\"}");
    forebay_walk *after_first = expect_step(&first, "{\"kind\":\"callProvider\",\"provider\":\"beta\","
                                            "\"model\":\"beta-large\"}", NULL, 1, label);
    forebay_walk *after_second = expect_step(&second, "{\"kind\":\"callProvider\",\"provider\":\"beta\","
                                             "\"model\":\"beta-large\"}", NULL, 1, label);
    release_walk(after_first);
    release_walk(after_second);
    release_walk(walk);
}

int main(void) {
    spisor_json_use_library_allocator();
    a_stream_failing_after_bytes_ends_with_an_ir_error_event();
    a_stream_failing_before_bytes_falls_to_the_next_entry();
    a_fallback_stream_failing_after_bytes_counts_as_served();
    no_running_provider_passes_through();
    a_decoded_pass_through_passes_through();
    a_served_first_entry_is_done_without_a_notice();
    a_thrown_entry_falls_through();
    a_fallback_serving_is_logged();
    the_last_entry_says_no_entry_is_left();
    a_plain_failure_is_answered_with_end_to_end_headers();
    a_rate_limited_entry_falls_through();
    exhaustion_after_a_limit_answers_rate_limited();
    the_latest_reset_and_the_last_headers_are_kept();
    milliseconds_win_over_retry_after_in_a_report();
    a_missing_provider_is_skipped_without_a_call();
    a_chain_of_missing_providers_never_calls_one();
    an_empty_chain_asks_for_a_refresh_once();
    resolve_notices_are_info_lines();
    a_refused_resolve_is_refused();
    the_same_walk_serves_two_reports();
    refuses_a_malformed_call();
    return support_summary("routing_step_test");
}
