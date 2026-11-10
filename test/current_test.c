#include "fixtures.h"

#define GAMMA_ENTRY(path) "\"gamma\":{\"id\":\"gamma\",\"label\":\"Gamma CLI\",\"home\":{\"candidates\":[\"" path "\"]}}"

static void expect_current(support_answer answer, const char *id, const char *label) {
    cJSON *expected = cJSON_CreateObject();
    cJSON_AddStringToObject(expected, "id", id);
    char *printed = cJSON_PrintUnformatted(expected);
    expect_json(&answer, 0, printed, label);
    free(printed);
    cJSON_Delete(expected);
}

static cJSON *quiet_context(const registry_case *open, const char *name, const char *value) {
    cJSON *context = name == NULL ? registry_context(open)
                                  : support_context(open->home, "HUB_APPS_FILE", open->file, name, value, NULL);
    support_arguments(context, "/usr/bin/node", "/test-runner/entry.js", NULL);
    return context;
}

static void answers_core_app_verbatim(void) {
    char *home = support_scratch();
    cJSON *context = support_context(home, "CORE_APP", "acme", NULL);
    support_arguments(context, NULL);
    expect_current(support_call(forebay_library_apps_current, "{}", context), "acme", "CORE_APP");
    cJSON *padded = support_context(home, "CORE_APP", "  acme \n", NULL);
    support_arguments(padded, NULL);
    expect_current(support_call(forebay_library_apps_current, "{}", padded), "acme", "CORE_APP trimmed");
    free(home);
}

static void reads_no_registry_when_core_app_is_set(void) {
    registry_case open = registry_case_open();
    support_write(open.file, "{\"gone\":null}");
    cJSON *context = support_context(open.home, "HUB_APPS_FILE", open.file, "CORE_APP", "acme", NULL);
    support_arguments(context, NULL);
    expect_current(support_call(forebay_library_apps_current, "{}", context), "acme", "CORE_APP before the registry");
    support_answer thrown = support_call(forebay_library_apps_current, "{}", quiet_context(&open, "CORE_APP", " "));
    expect_failure(&thrown, "a blank CORE_APP reads the registry");
    registry_case_close(&open);
}

static void matches_a_binary_as_an_argument_token(void) {
    registry_case open = registry_case_open();
    support_write(open.file, "{\"alpha\":" ALPHA ",\"beta\":" BETA "}");
    cJSON *context = registry_context(&open);
    support_arguments(context, "/usr/bin/node", "/usr/lib/node_modules/alpha-cli/bin/alpha.js", NULL);
    expect_current(support_call(forebay_library_apps_current, "{}", context), "alpha", "argv token");
    cJSON *upper = registry_context(&open);
    support_arguments(upper, "C:\\TOOLS\\BETA.EXE", NULL);
    expect_current(support_call(forebay_library_apps_current, "{}", upper), "beta", "argv token in another case");
    cJSON *longer = registry_context(&open);
    support_arguments(longer, "/bin/alphabet", "betamax", NULL);
    expect_current(support_call(forebay_library_apps_current, "{}", longer), "", "a token must match whole");
    cJSON *joined = registry_context(&open);
    support_arguments(joined, "x", "beta", "alpha", NULL);
    expect_current(support_call(forebay_library_apps_current, "{}", joined), "alpha", "the first app in registry order wins");
    registry_case_close(&open);
}

static void lowercases_a_declared_binary(void) {
    registry_case open = registry_case_open();
    support_write(open.file, "{\"k\":{\"label\":\"K\",\"home\":{\"candidates\":[\"/k\"]},\"detect\":{\"binary\":\"KiLo\"}}}");
    cJSON *context = registry_context(&open);
    support_arguments(context, "run", "kilo", NULL);
    expect_current(support_call(forebay_library_apps_current, "{}", context), "k", "uppercase binary");
    registry_case_close(&open);
}

static void fails_for_a_binary_that_is_not_a_string(void) {
    registry_case open = registry_case_open();
    support_write(open.file, "{\"alpha\":" ALPHA ",\"n\":{\"label\":\"N\",\"home\":{\"candidates\":[\"/n\"]},"
                             "\"detect\":{\"binary\":5}},\"f\":{\"label\":\"F\",\"home\":{\"candidates\":[\"/f\"]},"
                             "\"detect\":{\"binary\":0}}}");
    cJSON *context = registry_context(&open);
    support_arguments(context, "alpha", NULL);
    support_answer answer = support_call(forebay_library_apps_current, "{}", context);
    expect_failure(&answer, "every binary is read before a hit is taken");
    registry_case_close(&open);
}

static void matches_an_app_whose_native_variable_is_set(void) {
    registry_case open = registry_case_open();
    support_write(open.file, "{\"alpha\":" ALPHA ",\"beta\":" BETA "}");
    expect_current(support_call(forebay_library_apps_current, "{}", quiet_context(&open, "BETA_CONFIG_DIR", "/somewhere/beta")), "beta",
                   "nativeEnv");
    expect_current(support_call(forebay_library_apps_current, "{}", quiet_context(&open, "BETA_CONFIG_DIR", "  ")), "",
                   "a blank nativeEnv");
    registry_case_close(&open);
}

static void matches_a_native_variable_named_in_another_case_as_the_platform_does(void) {
    registry_case open = registry_case_open();
    support_write(open.file, "{\"alpha\":" ALPHA ",\"beta\":" BETA "}");
#ifdef _WIN32
    const char *expected = "beta";
#else
    const char *expected = "";
#endif
    expect_current(support_call(forebay_library_apps_current, "{}", quiet_context(&open, "Beta_Config_Dir", "/somewhere/beta")), expected,
                   "a nativeEnv spelled in another case");
    registry_case_close(&open);
}

static void matches_a_forced_directory_inside_a_candidate(void) {
    registry_case open = registry_case_open();
    char *candidate = support_join(open.home, "gamma-dir");
    cJSON *registry = cJSON_Parse("{\"alpha\":" ALPHA ",\"beta\":" BETA "}");
    cJSON *gamma = cJSON_Parse("{\"id\":\"gamma\",\"label\":\"Gamma CLI\",\"home\":{\"candidates\":[]},"
                               "\"detect\":{\"binary\":\"gamma\",\"pkg\":\"gamma-cli\"}}");
    cJSON_AddItemToArray(cJSON_GetObjectItem(cJSON_GetObjectItem(gamma, "home"), "candidates"),
                         cJSON_CreateString(candidate));
    cJSON_AddItemToObject(registry, "gamma", gamma);
    char *text = cJSON_PrintUnformatted(registry);
    support_write(open.file, text);
    char *forced = support_join3(open.home, "gamma-dir", "nested");
    expect_current(support_call(forebay_library_apps_current, "{}", quiet_context(&open, "HUB_CONFIG_DIR", forced)), "gamma",
                   "forced inside a candidate");
    char *shouted = malloc(strlen(candidate) + 8);
    sprintf(shouted, "%s\\//", candidate);
    for (char *cursor = shouted; *cursor != '\0'; cursor++) {
        if (*cursor >= 'a' && *cursor <= 'z') {
            *cursor = (char)(*cursor - 'a' + 'A');
        }
    }
    expect_current(support_call(forebay_library_apps_current, "{}", quiet_context(&open, "HUB_CONFIG_DIR", shouted)), "gamma",
                   "normalised case, separators and trailing slashes");
    free(shouted);
    free(forced);
    free(text);
    cJSON_Delete(registry);
    free(candidate);
    registry_case_close(&open);
}

static void matches_a_candidate_inside_a_forced_directory(void) {
    registry_case open = registry_case_open();
    support_write(open.file, "{" GAMMA_ENTRY("/root/apps/gamma") "}");
    expect_current(support_call(forebay_library_apps_current, "{}", quiet_context(&open, "HUB_CONFIG_DIR", "/root/apps")), "gamma",
                   "candidate inside forced");
    expect_current(support_call(forebay_library_apps_current, "{}", quiet_context(&open, "HUB_CONFIG_DIR", "/root/app")), "",
                   "a partial segment is no prefix");
    registry_case_close(&open);
}

static void matches_a_forced_directory_by_its_xdg_subdirectory(void) {
    registry_case open = registry_case_open();
    support_write(open.file, "{\"alpha\":" ALPHA ",\"beta\":" BETA "}");
    expect_current(support_call(forebay_library_apps_current, "{}", quiet_context(&open, "HUB_CONFIG_DIR", "/custom/xdg/BETA/")), "beta",
                   "xdg suffix");
    registry_case_close(&open);
}

static void matches_a_forced_directory_by_its_last_segment_for_an_app_with_a_native_variable(void) {
    registry_case open = registry_case_open();
    support_write(open.file, "{\"alpha\":" ALPHA "," GAMMA_ENTRY("/g") "}");
    expect_current(support_call(forebay_library_apps_current, "{}", quiet_context(&open, "HUB_CONFIG_DIR", "D:\\Work\\ALPHA")), "alpha",
                   "last segment names an app with nativeEnv");
    expect_current(support_call(forebay_library_apps_current, "{}", quiet_context(&open, "HUB_CONFIG_DIR", "/work/gamma")), "",
                   "last segment of an app without nativeEnv");
    registry_case_close(&open);
}

static void fails_for_a_candidate_that_is_not_a_string_unless_an_earlier_app_matched(void) {
    registry_case open = registry_case_open();
    support_write(open.file, "{" GAMMA_ENTRY("/g") ",\"n\":{\"label\":\"N\",\"home\":{\"candidates\":[3]}}}");
    expect_current(support_call(forebay_library_apps_current, "{}", quiet_context(&open, "HUB_CONFIG_DIR", "/g/x")), "gamma",
                   "find stops at the first match");
    support_answer thrown = support_call(forebay_library_apps_current, "{}", quiet_context(&open, "HUB_CONFIG_DIR", "/elsewhere"));
    expect_failure(&thrown, "a number candidate is expanded");
    registry_case_close(&open);
}

static void answers_an_empty_id_when_nothing_matches(void) {
    registry_case open = registry_case_open();
    support_write(open.file, "{\"alpha\":" ALPHA ",\"beta\":" BETA "}");
    cJSON *context = registry_context(&open);
    support_arguments(context, "/usr/bin/node", "/usr/lib/node_modules/unrelated/cli.js", NULL);
    expect_current(support_call(forebay_library_apps_current, "{}", context), "", "nothing matches");
    registry_case_close(&open);
}

int main(void) {
    answers_core_app_verbatim();
    reads_no_registry_when_core_app_is_set();
    matches_a_binary_as_an_argument_token();
    lowercases_a_declared_binary();
    fails_for_a_binary_that_is_not_a_string();
    matches_an_app_whose_native_variable_is_set();
    matches_a_native_variable_named_in_another_case_as_the_platform_does();
    matches_a_forced_directory_inside_a_candidate();
    matches_a_candidate_inside_a_forced_directory();
    matches_a_forced_directory_by_its_xdg_subdirectory();
    matches_a_forced_directory_by_its_last_segment_for_an_app_with_a_native_variable();
    fails_for_a_candidate_that_is_not_a_string_unless_an_earlier_app_matched();
    answers_an_empty_id_when_nothing_matches();
    return support_summary("current_test");
}
