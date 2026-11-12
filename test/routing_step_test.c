#include "support.h"

int32_t forebay_library_routing_step(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);

#define REQUESTED "acme-large-1"
#define WORDING "{\"loginCommand\":\"host-cli action session\",\"settingsFile\":\"/home/settings.json\"}"
#define TWO_ENTRIES                                                                                        \
    "{\"large\":[{\"provider\":\"alpha\",\"model\":\"alpha-large\",\"name\":\"Alpha Large\",\"derived\":false}," \
    "{\"provider\":\"beta\",\"model\":\"beta-large\",\"name\":\"Beta Large\",\"derived\":false}]}"
#define BOTH_RUNNING "[\"alpha\",\"beta\"]"

static support_answer first_step(const char *profile, const char *map, const char *providers, const char *request,
                                 int refreshed) {
    char input[4096];
    snprintf(input, sizeof(input),
             "{\"profile\":%s,\"map\":%s,\"models\":{},\"providers\":%s,\"request\":%s,\"refreshed\":%s,"
             "\"wording\":%s}",
             profile, map, providers, request, refreshed ? "true" : "false", WORDING);
    return support_call_raw(forebay_library_routing_step, input);
}

static support_answer requested_step(const char *map, const char *providers) {
    return first_step("{}", map, providers, "{\"model\":\"" REQUESTED "\"}", 0);
}

static support_answer reported_step(const cJSON *walk, const char *providers, const char *report) {
    char *walk_text = cJSON_PrintUnformatted(walk);
    char input[4096];
    snprintf(input, sizeof(input), "{\"walk\":%s,\"providers\":%s,\"report\":%s}", walk_text, providers, report);
    free(walk_text);
    return support_call_raw(forebay_library_routing_step, input);
}

static void expect_member(const cJSON *result, const char *name, const char *expected, const char *label,
                          const char *text) {
    cJSON *wanted = cJSON_Parse(expected);
    const cJSON *actual = cJSON_GetObjectItemCaseSensitive(result, name);
    if (wanted == NULL || !cJSON_Compare(actual, wanted, 1)) {
        support_failf(label, "%s differs\n  expected %s\n  answer   %s", name, expected, text);
    }
    cJSON_Delete(wanted);
}

/** Checks the step and the log, and that a walk is present exactly when expected; answers the walk, detached. */
static cJSON *expect_step(support_answer *answer, const char *step, const char *log, int expects_walk,
                          const char *label) {
    cJSON *walk = NULL;
    if (answer->status != 0 || answer->result == NULL) {
        support_failf(label, "status %d: %s", (int)answer->status, answer->text);
    } else {
        expect_member(answer->result, "step", step, label, answer->text);
        expect_member(answer->result, "log", log, label, answer->text);
        walk = cJSON_DetachItemFromObjectCaseSensitive(answer->result, "walk");
        if ((walk != NULL) != expects_walk) {
            support_failf(label, "expected %s walk: %s", expects_walk ? "a" : "no", answer->text);
        }
    }
    support_discard(answer);
    return walk;
}

static void expect_refused(support_answer *answer, const char *label) {
    const cJSON *refused = cJSON_GetObjectItemCaseSensitive(answer->result, "refused");
    const cJSON *fix = cJSON_GetObjectItemCaseSensitive(answer->result, "fix");
    if (answer->status != 0 || !cJSON_IsString(refused) || !cJSON_IsString(fix) ||
        cJSON_GetObjectItemCaseSensitive(answer->result, "step") != NULL) {
        support_failf(label, "expected a refusal, got status %d: %s", (int)answer->status, answer->text);
    }
    support_discard(answer);
}

static cJSON *walk_calling_alpha(const char *label) {
    support_answer first = requested_step(TWO_ENTRIES, BOTH_RUNNING);
    return expect_step(&first, "{\"kind\":\"callProvider\",\"provider\":\"alpha\",\"model\":\"alpha-large\"}", "[]",
                       1, label);
}

static void a_stream_failing_after_bytes_ends_with_an_ir_error_event(void) {
    const char *label = "a_stream_failing_after_bytes_ends_with_an_ir_error_event";
    cJSON *walk = walk_calling_alpha(label);
    support_answer ended = reported_step(walk, BOTH_RUNNING,
                                         "{\"kind\":\"streamFailed\",\"afterBytes\":true,\"message\":\"socket closed\"}");
    cJSON_Delete(expect_step(&ended,
                             "{\"kind\":\"endStream\",\"event\":{\"event\":\"error\",\"errorType\":\"api_error\","
                             "\"message\":\"socket closed\"}}",
                             "[{\"level\":\"warn\",\"message\":\"a stream failed after its first event: socket closed\"}]",
                             0, label));
    cJSON_Delete(walk);
}

static void a_stream_failing_before_bytes_falls_to_the_next_entry(void) {
    const char *label = "a_stream_failing_before_bytes_falls_to_the_next_entry";
    cJSON *walk = walk_calling_alpha(label);
    support_answer next = reported_step(walk, BOTH_RUNNING,
                                        "{\"kind\":\"streamFailed\",\"afterBytes\":false,\"message\":\"no first event\"}");
    cJSON_Delete(expect_step(&next, "{\"kind\":\"callProvider\",\"provider\":\"beta\",\"model\":\"beta-large\"}",
                             "[{\"level\":\"warn\",\"message\":\"the stream of alpha-large failed on its first event, "
                             "so the next chain entry serves " REQUESTED ": no first event\"}]",
                             1, label));
    cJSON_Delete(walk);
}

static void a_fallback_stream_failing_after_bytes_counts_as_served(void) {
    const char *label = "a_fallback_stream_failing_after_bytes_counts_as_served";
    cJSON *walk = walk_calling_alpha(label);
    support_answer next = reported_step(walk, BOTH_RUNNING, "{\"kind\":\"thrown\",\"message\":\"refused\"}");
    cJSON *second = expect_step(&next, "{\"kind\":\"callProvider\",\"provider\":\"beta\",\"model\":\"beta-large\"}",
                                "[{\"level\":\"warn\",\"message\":\"alpha failed alpha-large, so the next chain entry "
                                "serves " REQUESTED ": refused\"}]",
                                1, label);
    support_answer ended = reported_step(second, BOTH_RUNNING,
                                         "{\"kind\":\"streamFailed\",\"afterBytes\":true,\"message\":\"cut\"}");
    cJSON_Delete(expect_step(&ended,
                             "{\"kind\":\"endStream\",\"event\":{\"event\":\"error\",\"errorType\":\"api_error\","
                             "\"message\":\"cut\"}}",
                             "[{\"level\":\"warn\",\"message\":\"a stream failed after its first event: cut\"},"
                             "{\"level\":\"warn\",\"message\":\"beta-large served " REQUESTED
                             " in place of alpha-large\"}]",
                             0, label));
    cJSON_Delete(second);
    cJSON_Delete(walk);
}

static void no_running_provider_passes_through(void) {
    support_answer answer = requested_step(TWO_ENTRIES, "[]");
    cJSON_Delete(expect_step(&answer, "{\"kind\":\"passThrough\"}", "[]", 0, "no_running_provider_passes_through"));
}

static void a_decoded_pass_through_passes_through(void) {
    support_answer answer = first_step("{}", TWO_ENTRIES, BOTH_RUNNING, "{\"passThrough\":true}", 0);
    cJSON_Delete(expect_step(&answer, "{\"kind\":\"passThrough\"}", "[]", 0, "a_decoded_pass_through_passes_through"));
}

static void a_served_first_entry_is_done_without_a_notice(void) {
    const char *label = "a_served_first_entry_is_done_without_a_notice";
    cJSON *walk = walk_calling_alpha(label);
    support_answer response = reported_step(walk, BOTH_RUNNING, "{\"kind\":\"response\"}");
    cJSON_Delete(expect_step(&response, "{\"kind\":\"done\"}", "[]", 0, label));
    support_answer streamed = reported_step(walk, BOTH_RUNNING, "{\"kind\":\"streamed\"}");
    cJSON_Delete(expect_step(&streamed, "{\"kind\":\"done\"}", "[]", 0, label));
    cJSON_Delete(walk);
}

static cJSON *walk_after_alpha_throws(const char *label) {
    cJSON *walk = walk_calling_alpha(label);
    support_answer next = reported_step(walk, BOTH_RUNNING, "{\"kind\":\"thrown\",\"message\":\"boom\"}");
    cJSON *second = expect_step(&next, "{\"kind\":\"callProvider\",\"provider\":\"beta\",\"model\":\"beta-large\"}",
                                "[{\"level\":\"warn\",\"message\":\"alpha failed alpha-large, so the next chain entry "
                                "serves " REQUESTED ": boom\"}]",
                                1, label);
    cJSON_Delete(walk);
    return second;
}

static void a_thrown_entry_falls_through(void) {
    cJSON_Delete(walk_after_alpha_throws("a_thrown_entry_falls_through"));
}

static void a_fallback_serving_is_logged(void) {
    const char *label = "a_fallback_serving_is_logged";
    cJSON *walk = walk_after_alpha_throws(label);
    support_answer served = reported_step(walk, BOTH_RUNNING, "{\"kind\":\"response\"}");
    cJSON_Delete(expect_step(&served, "{\"kind\":\"done\"}",
                             "[{\"level\":\"warn\",\"message\":\"beta-large served " REQUESTED
                             " in place of alpha-large\"}]",
                             0, label));
    cJSON_Delete(walk);
}

static void the_last_entry_says_no_entry_is_left(void) {
    const char *label = "the_last_entry_says_no_entry_is_left";
    cJSON *walk = walk_after_alpha_throws(label);
    support_answer exhausted = reported_step(walk, BOTH_RUNNING, "{\"kind\":\"thrown\",\"message\":\"bust\"}");
    cJSON_Delete(expect_step(&exhausted,
                             "{\"kind\":\"reply\",\"reply\":{\"kind\":\"proxyError\",\"status\":502,"
                             "\"message\":\"no chain entry could serve the request: bust\"}}",
                             "[{\"level\":\"warn\",\"message\":\"beta failed beta-large, and no chain entry is left to "
                             "serve " REQUESTED ": bust\"}]",
                             0, label));
    cJSON_Delete(walk);
}

static void a_plain_failure_is_answered_with_end_to_end_headers(void) {
    const char *label = "a_plain_failure_is_answered_with_end_to_end_headers";
    cJSON *walk = walk_calling_alpha(label);
    support_answer failed = reported_step(walk, BOTH_RUNNING,
                                          "{\"kind\":\"failure\",\"status\":500,\"headers\":{\"content-type\":"
                                          "\"application/json\",\"Connection\":\"close\",\"Content-Length\":\"12\","
                                          "\"content-encoding\":\"gzip\",\"x-trace\":\"t\"},\"now\":1000}");
    cJSON_Delete(expect_step(&failed,
                             "{\"kind\":\"reply\",\"reply\":{\"kind\":\"providerFailure\",\"status\":500,"
                             "\"headers\":{\"content-type\":\"application/json\",\"x-trace\":\"t\"}}}",
                             "[]", 0, label));
    cJSON_Delete(walk);
}

static cJSON *walk_after_alpha_limited(const char *report, const char *label) {
    cJSON *walk = walk_calling_alpha(label);
    support_answer next = reported_step(walk, BOTH_RUNNING, report);
    cJSON *second = expect_step(&next, "{\"kind\":\"callProvider\",\"provider\":\"beta\",\"model\":\"beta-large\"}",
                                "[{\"level\":\"warn\",\"message\":\"alpha-large is rate limited, so the next chain "
                                "entry serves " REQUESTED "\"}]",
                                1, label);
    cJSON_Delete(walk);
    return second;
}

static void a_rate_limited_entry_falls_through(void) {
    cJSON_Delete(walk_after_alpha_limited("{\"kind\":\"failure\",\"status\":429,\"headers\":{},\"now\":1000}",
                                          "a_rate_limited_entry_falls_through"));
}

static void expect_exhausted_reply(cJSON *walk, const char *report, const char *reply, const char *log,
                                   const char *label) {
    support_answer exhausted = reported_step(walk, BOTH_RUNNING, report);
    cJSON_Delete(expect_step(&exhausted, reply, log, 0, label));
    cJSON_Delete(walk);
}

static void exhaustion_after_a_limit_answers_rate_limited(void) {
    const char *label = "exhaustion_after_a_limit_answers_rate_limited";
    cJSON *walk = walk_after_alpha_limited(
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
    cJSON *walk = walk_after_alpha_limited("{\"kind\":\"failure\",\"status\":429,\"headers\":{\"retry-after\":\"30\"},"
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
    support_answer answer = requested_step(TWO_ENTRIES, "[\"beta\"]");
    cJSON_Delete(expect_step(&answer, "{\"kind\":\"callProvider\",\"provider\":\"beta\",\"model\":\"beta-large\"}",
                             "[{\"level\":\"warn\",\"message\":\"no running plugin provides alpha, which the model map "
                             "names for alpha-large\"}]",
                             1, "a_missing_provider_is_skipped_without_a_call"));
}

static void a_chain_of_missing_providers_never_calls_one(void) {
    support_answer answer = requested_step(TWO_ENTRIES, "[\"gamma\"]");
    cJSON_Delete(expect_step(&answer,
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
    support_answer first = requested_step("{}", BOTH_RUNNING);
    cJSON_Delete(expect_step(&first, "{\"kind\":\"refreshModels\"}", switched, 0, label));
    support_answer refreshed = first_step("{}", "{}", BOTH_RUNNING, "{\"model\":\"" REQUESTED "\"}", 1);
    cJSON_Delete(expect_step(&refreshed,
                             "{\"kind\":\"reply\",\"reply\":{\"kind\":\"apiError\",\"status\":503,"
                             "\"message\":\"no running provider offers a model for " REQUESTED "; log a provider in "
                             "with host-cli action session alpha login or host-cli action session beta login, or set "
                             "routing.modelMap in /home/settings.json\"}}",
                             switched, 0, label));
}

static void resolve_notices_are_info_lines(void) {
    support_answer answer = requested_step(
        "{\"default\":[{\"provider\":\"alpha\",\"model\":\"alpha-small\",\"name\":\"Alpha Small\",\"derived\":true}]}",
        BOTH_RUNNING);
    cJSON_Delete(expect_step(&answer, "{\"kind\":\"callProvider\",\"provider\":\"alpha\",\"model\":\"alpha-small\"}",
                             "[{\"level\":\"info\",\"message\":\"routing " REQUESTED ": model_switched\"},"
                             "{\"level\":\"info\",\"message\":\"routing " REQUESTED ": healed\"}]",
                             1, "resolve_notices_are_info_lines"));
}

static void a_refused_resolve_is_refused(void) {
    support_answer answer = first_step("{\"nativeModelPattern\":\"(a|b)\"}", TWO_ENTRIES, BOTH_RUNNING,
                                       "{\"model\":\"" REQUESTED "\"}", 0);
    expect_refused(&answer, "a_refused_resolve_is_refused");
}

static void expect_walk_refused(const char *walk, const char *label) {
    cJSON *altered = cJSON_Parse(walk);
    support_answer answer = reported_step(altered, BOTH_RUNNING, "{\"kind\":\"response\"}");
    expect_refused(&answer, label);
    cJSON_Delete(altered);
}

static void an_altered_walk_is_refused(void) {
    expect_walk_refused("\"not a walk\"", "an_altered_walk_is_refused: not an object");
    expect_walk_refused("{\"requested\":\"m\",\"chain\":[{\"provider\":\"alpha\",\"model\":\"alpha-large\"}],"
                        "\"index\":1}",
                        "an_altered_walk_is_refused: an index past the chain");
    expect_walk_refused("{\"requested\":\"m\",\"chain\":\"alpha\",\"index\":0}",
                        "an_altered_walk_is_refused: a chain that is not an array");
    expect_walk_refused("{\"requested\":\"m\",\"chain\":[{\"provider\":\"alpha\",\"model\":\"alpha-large\"}],"
                        "\"index\":0,\"limited\":\"soon\"}",
                        "an_altered_walk_is_refused: a limited member that is not an object");
    expect_walk_refused("{\"requested\":\"m\",\"chain\":[{\"provider\":\"alpha\",\"model\":\"alpha-large\"}],"
                        "\"index\":0,\"limited\":{\"headers\":[]}}",
                        "an_altered_walk_is_refused: limited headers that are not an object");
    expect_walk_refused("{\"requested\":\"m\",\"chain\":[{\"provider\":\"alpha\",\"model\":\"alpha-large\"}],"
                        "\"index\":0,\"limited\":{\"headers\":{},\"resetAt\":\"later\"}}",
                        "an_altered_walk_is_refused: a limited reset that is not a number");
    expect_walk_refused("{\"requested\":\"m\",\"chain\":[{\"provider\":\"alpha\",\"model\":\"alpha-large\"}],"
                        "\"index\":0,\"limited\":{\"headers\":{},\"resetAt\":0}}",
                        "an_altered_walk_is_refused: a limited reset of 0");
    expect_walk_refused("{\"requested\":\"m\",\"chain\":[\"alpha\"],\"index\":0}",
                        "an_altered_walk_is_refused: a chain entry that is not an object");
    expect_walk_refused("{\"requested\":\"m\",\"chain\":[{\"provider\":1,\"model\":\"alpha-large\"}],\"index\":0}",
                        "an_altered_walk_is_refused: a chain entry lacking a string provider");
    expect_walk_refused("{\"requested\":\"m\",\"chain\":[{\"provider\":\"alpha\",\"model\":\"alpha-large\"}],"
                        "\"index\":-1}",
                        "an_altered_walk_is_refused: a negative index");
    expect_walk_refused("{\"requested\":\"m\",\"chain\":[{\"provider\":\"alpha\",\"model\":\"alpha-large\"},"
                        "{\"provider\":\"beta\",\"model\":\"beta-large\"}],\"index\":0.5}",
                        "an_altered_walk_is_refused: a fractional index");
    expect_walk_refused("{\"requested\":7,\"chain\":[{\"provider\":\"alpha\",\"model\":\"alpha-large\"}],\"index\":0}",
                        "an_altered_walk_is_refused: a requested model that is not a string");
    expect_walk_refused("{\"requested\":\"m\",\"chain\":[{\"provider\":\"alpha\",\"model\":\"alpha-large\"}],"
                        "\"index\":0,\"failure\":false}",
                        "an_altered_walk_is_refused: a failure that is not a string");
}

static void refuses_a_malformed_call(void) {
    support_answer no_providers = support_call_raw(forebay_library_routing_step, "{\"request\":{\"passThrough\":true}}");
    expect_failure(&no_providers, "step without providers");
    cJSON *walk = walk_calling_alpha("refuses_a_malformed_call");
    support_answer no_message = reported_step(walk, BOTH_RUNNING, "{\"kind\":\"thrown\"}");
    expect_failure(&no_message, "step with a thrown report lacking its message");
    support_answer no_status = reported_step(walk, BOTH_RUNNING, "{\"kind\":\"failure\",\"headers\":{},\"now\":1}");
    expect_failure(&no_status, "step with a failure report lacking its status");
    support_answer no_now = reported_step(walk, BOTH_RUNNING, "{\"kind\":\"failure\",\"status\":429,\"headers\":{}}");
    expect_failure(&no_now, "step with a failure report lacking its clock");
    support_answer unknown = reported_step(walk, BOTH_RUNNING, "{\"kind\":\"vanished\"}");
    expect_refused(&unknown, "step with a report kind it does not read");
    cJSON_Delete(walk);
}

int main(void) {
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
    an_altered_walk_is_refused();
    refuses_a_malformed_call();
    return support_summary("routing_step_test");
}
