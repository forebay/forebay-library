#include "fixtures.h"

static void expect_home(support_answer *answer, const char *path, const char *label) {
    cJSON *expected = cJSON_CreateObject();
    cJSON_AddStringToObject(expected, "home", path);
    char *printed = cJSON_PrintUnformatted(expected);
    expect_json(answer, 0, printed, label);
    free(printed);
    cJSON_Delete(expected);
}

static support_answer home_of(const registry_case *open, const char *id, const char *name, const char *value) {
    char argument[256];
    snprintf(argument, sizeof(argument), "{\"id\":\"%s\"}", id);
    cJSON *context = name == NULL ? registry_context(open)
                                  : support_context(open->home, "HUB_APPS_FILE", open->file, name, value, NULL);
    return support_call(forebay_library_apps_home, argument, context);
}

static void honors_the_override_then_the_native_variable_then_xdg(void) {
    registry_case open = registry_case_open();
    support_write(open.file, "{\"beta\":" BETA "}");
    support_answer override = home_of(&open, "beta", "HUB_BETA_DIR", "/b");
    expect_home(&override, "/b", "envOverride");
    support_answer native = home_of(&open, "beta", "BETA_CONFIG_DIR", "/bc");
    expect_home(&native, "/bc", "nativeEnv");
    support_answer xdg = home_of(&open, "beta", "XDG_CONFIG_HOME", "/xdg");
    char *joined = support_join("/xdg", "beta");
    expect_home(&xdg, joined, "xdg");
    free(joined);
    support_answer relative = home_of(&open, "beta", "XDG_CONFIG_HOME", "rel/");
    joined = support_join("rel/", "beta");
    expect_home(&relative, joined, "a relative xdg");
    free(joined);
    registry_case_close(&open);
}

static void ranks_the_override_above_the_native_variable(void) {
    registry_case open = registry_case_open();
    support_write(open.file, "{\"beta\":" BETA "}");
    cJSON *context = support_context(open.home, "HUB_APPS_FILE", open.file, "HUB_BETA_DIR", " /b ", "BETA_CONFIG_DIR",
                                     "/bc", "XDG_CONFIG_HOME", "/xdg", NULL);
    support_answer answer = support_call(forebay_library_apps_home, "{\"id\":\"beta\"}", context);
    expect_home(&answer, "/b", "override first, trimmed");
    cJSON *blank = support_context(open.home, "HUB_APPS_FILE", open.file, "HUB_BETA_DIR", "  ", "BETA_CONFIG_DIR",
                                   "/bc", NULL);
    support_answer skipped = support_call(forebay_library_apps_home, "{\"id\":\"beta\"}", blank);
    expect_home(&skipped, "/bc", "a blank override is skipped");
    registry_case_close(&open);
}

static void answers_the_first_existing_candidate_else_the_last(void) {
    registry_case open = registry_case_open();
    support_write(open.file, "{\"alpha\":" ALPHA "}");
    support_answer none = home_of(&open, "alpha", NULL, NULL);
    char *last = support_join3(open.home, ".config", "alpha");
    expect_home(&none, last, "last candidate");
    char *first = support_join(open.home, ".alpha");
    support_make_directory(first);
    support_answer existing = home_of(&open, "alpha", NULL, NULL);
    expect_home(&existing, first, "first existing candidate");
    free(first);
    free(last);
    registry_case_close(&open);
}

static void expands_every_home_marker(void) {
    registry_case open = registry_case_open();
    support_write(open.file, "{\"t\":{\"label\":\"T\",\"home\":{\"candidates\":[\"~\"]}},"
                             "\"b\":{\"label\":\"B\",\"home\":{\"candidates\":[\"~\\\\back\"]}},"
                             "\"p\":{\"label\":\"P\",\"home\":{\"candidates\":[\"/plain/../x\"]}}}");
    support_answer tilde = home_of(&open, "t", NULL, NULL);
    expect_home(&tilde, open.home, "a bare tilde");
    support_answer back = home_of(&open, "b", NULL, NULL);
    char *joined = support_join(open.home, "back");
    expect_home(&back, joined, "a backslash after the tilde");
    free(joined);
    support_answer plain = home_of(&open, "p", NULL, NULL);
    expect_home(&plain, "/plain/../x", "a plain candidate is not normalised");
    registry_case_close(&open);
}

static void answers_nothing_for_an_unknown_id(void) {
    registry_case open = registry_case_open();
    support_write(open.file, "{\"alpha\":" ALPHA "}");
    support_answer answer = home_of(&open, "zeta", NULL, NULL);
    expect_json(&answer, 0, "{}", "unknown id");
    registry_case_close(&open);
}

static void fails_for_a_candidate_that_is_not_a_string(void) {
    registry_case open = registry_case_open();
    support_write(open.file, "{\"n\":{\"label\":\"N\",\"home\":{\"envOverride\":\"HUB_N_DIR\",\"candidates\":[\"/a\",5]}}}");
    support_answer answer = home_of(&open, "n", NULL, NULL);
    expect_failure(&answer, "number candidate");
    support_answer overridden = home_of(&open, "n", "HUB_N_DIR", "/over");
    expect_home(&overridden, "/over", "the override is read before the candidates");
    registry_case_close(&open);
}

static void fails_for_an_xdg_subdirectory_that_is_not_a_string_only_when_xdg_is_set(void) {
    registry_case open = registry_case_open();
    support_write(open.file, "{\"x\":{\"label\":\"X\",\"home\":{\"xdgSubdir\":5,\"candidates\":[\"/c\"]}}}");
    support_answer joined = home_of(&open, "x", "XDG_CONFIG_HOME", "/xdg");
    expect_failure(&joined, "number xdgSubdir with xdg set");
    support_answer unread = home_of(&open, "x", NULL, NULL);
    expect_home(&unread, "/c", "number xdgSubdir without xdg");
    registry_case_close(&open);
}

static void reads_a_variable_named_by_any_value_as_javascript_keys_it(void) {
    registry_case open = registry_case_open();
    support_write(open.file,
                  "{\"n\":{\"label\":\"N\",\"home\":{\"envOverride\":5,\"candidates\":[\"/c\"]}},"
                  "\"f\":{\"label\":\"F\",\"home\":{\"envOverride\":1.5e-7,\"candidates\":[\"/c\"]}},"
                  "\"g\":{\"label\":\"G\",\"home\":{\"envOverride\":1e21,\"candidates\":[\"/c\"]}},"
                  "\"h\":{\"label\":\"H\",\"home\":{\"envOverride\":123.456,\"candidates\":[\"/c\"]}},"
                  "\"t\":{\"label\":\"T\",\"home\":{\"envOverride\":true,\"candidates\":[\"/c\"]}},"
                  "\"a\":{\"label\":\"A\",\"home\":{\"envOverride\":[\"P\",null,[1,\"Q\"]],\"candidates\":[\"/c\"]}},"
                  "\"o\":{\"label\":\"O\",\"home\":{\"envOverride\":{},\"candidates\":[\"/c\"]}},"
                  "\"z\":{\"label\":\"Z\",\"home\":{\"envOverride\":0,\"nativeEnv\":\"0\",\"candidates\":[\"/c\"]}}}");
    support_answer number = home_of(&open, "n", "5", "/five");
    expect_home(&number, "/five", "a number names its decimal form");
    support_answer small = home_of(&open, "f", "1.5e-7", "/small");
    expect_home(&small, "/small", "a small fraction names its exponent form");
    support_answer large = home_of(&open, "g", "1e+21", "/large");
    expect_home(&large, "/large", "a large number names its exponent form");
    support_answer fraction = home_of(&open, "h", "123.456", "/fraction");
    expect_home(&fraction, "/fraction", "a fraction names its shortest form");
    support_answer boolean = home_of(&open, "t", "true", "/true");
    expect_home(&boolean, "/true", "true names true");
    support_answer array = home_of(&open, "a", "P,,1,Q", "/array");
    expect_home(&array, "/array", "an array names its join");
    support_answer object = home_of(&open, "o", "[object Object]", "/object");
    expect_home(&object, "/object", "an object names [object Object]");
    support_answer zero = home_of(&open, "z", "0", "/zero");
    expect_home(&zero, "/zero", "a zero override is falsy, the string native is not");
    registry_case_close(&open);
}

static void fails_for_an_unset_variable_named_after_an_object_member(void) {
    registry_case open = registry_case_open();
    support_write(open.file, "{\"p\":{\"label\":\"P\",\"home\":{\"envOverride\":\"toString\",\"candidates\":[\"/c\"]}},"
                             "\"q\":{\"label\":\"Q\",\"home\":{\"nativeEnv\":\"__proto__\",\"candidates\":[\"/c\"]}}}");
    support_answer unset = home_of(&open, "p", NULL, NULL);
    expect_failure(&unset, "toString unset");
    support_answer set = home_of(&open, "p", "toString", "/own");
    expect_home(&set, "/own", "toString set");
    support_answer proto = home_of(&open, "q", NULL, NULL);
    expect_failure(&proto, "__proto__ unset");
    registry_case_close(&open);
}

int main(void) {
    honors_the_override_then_the_native_variable_then_xdg();
    ranks_the_override_above_the_native_variable();
    answers_the_first_existing_candidate_else_the_last();
    expands_every_home_marker();
    answers_nothing_for_an_unknown_id();
    fails_for_a_candidate_that_is_not_a_string();
    fails_for_an_xdg_subdirectory_that_is_not_a_string_only_when_xdg_is_set();
    reads_a_variable_named_by_any_value_as_javascript_keys_it();
    fails_for_an_unset_variable_named_after_an_object_member();
    return support_summary("home_test");
}
