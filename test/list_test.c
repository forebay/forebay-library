#include "fixtures.h"

static void answers_an_empty_list_when_the_registry_is_absent(void) {
    registry_case open = registry_case_open();
    support_answer answer = support_call(forebay_library_apps_list, "{}", registry_context(&open));
    expect_json(&answer, 0, "{\"apps\":[]}", "absent registry");
    registry_case_close(&open);
}

static void answers_an_empty_list_when_the_registry_is_empty(void) {
    support_answer answer = list_text("{}");
    expect_json(&answer, 0, "{\"apps\":[]}", "empty registry");
}

static void reads_descriptors_solely_from_the_registry(void) {
    support_answer answer = list_text("{\"alpha\":" ALPHA ",\"beta\":" BETA "}");
    expect_at(&answer, "apps.0.detect.pkg", "\"alpha-cli\"", "alpha pkg");
    expect_at(&answer, "apps.0.loader", "{\"id\":\"alpha-loader\",\"url\":\"acme-org/alpha-loader\"}", "alpha loader");
    expect_ids(&answer, "alpha,beta", "both apps");
}

static void resolves_a_whole_descriptor_with_its_defaults(void) {
    registry_case open = registry_case_open();
    support_write(open.file, "{\"alpha\":" ALPHA "}");
    support_answer answer = support_call(forebay_library_apps_get, "{\"id\":\"alpha\"}", registry_context(&open));
    expect_json(&answer, 0,
                "{\"app\":{\"id\":\"alpha\",\"label\":\"Alpha CLI\",\"home\":{\"envOverride\":\"HUB_ALPHA_DIR\","
                "\"nativeEnv\":\"ALPHA_CONFIG_DIR\",\"candidates\":[\"~/.alpha\",\"~/.config/alpha\"]},"
                "\"detect\":{\"binary\":\"alpha\",\"pkg\":\"alpha-cli\"},\"loader\":{\"id\":\"alpha-loader\","
                "\"url\":\"acme-org/alpha-loader\"},\"commandsSubdir\":\"commands\",\"paths\":{\"repos\":\"repos\","
                "\"plugin\":\"plugin\",\"cache\":\"cache\",\"config\":\"config\"},\"proxyPort\":40001,"
                "\"integration\":\"env-baseurl\",\"wireFormat\":\"anthropic\",\"usage\":{\"formats\":"
                "[\"alpha-jsonl\"]}}}",
                "alpha resolved");
    registry_case_close(&open);
}

static void defaults_an_entry_id_from_its_key(void) {
    support_answer answer = list_text("{\"alpha\":{\"label\":\"Alpha\",\"home\":{\"candidates\":[\"~/.alpha\"]}}}");
    expect_ids(&answer, "alpha", "id from key");
}

static void defaults_the_binary_and_the_other_optional_fields(void) {
    support_answer answer = list_text("{\"gamma\":{\"id\":\"gamma\",\"label\":\"Gamma\",\"home\":{\"candidates\":[\"/g\"]}}}");
    expect_json(&answer, 0,
                "{\"apps\":[{\"id\":\"gamma\",\"label\":\"Gamma\",\"home\":{\"candidates\":[\"/g\"]},"
                "\"detect\":{\"binary\":\"gamma\",\"pkg\":\"\"},\"commandsSubdir\":\"commands\",\"paths\":"
                "{\"repos\":\"repos\",\"plugin\":\"plugin\",\"cache\":\"cache\",\"config\":\"config\"},"
                "\"proxyPort\":0,\"integration\":\"env-baseurl\",\"wireFormat\":\"anthropic\"}]}",
                "defaults");
}

static void answers_an_empty_list_on_a_malformed_registry(void) {
    support_answer answer = list_text("{ not json");
    expect_json(&answer, 0, "{\"apps\":[]}", "malformed registry");
}

static void answers_an_empty_list_when_the_registry_is_not_an_object(void) {
    support_answer array = list_text("[" ALPHA "]");
    expect_json(&array, 0, "{\"apps\":[]}", "array registry");
    support_answer null_registry = list_text("null");
    expect_json(&null_registry, 0, "{\"apps\":[]}", "null registry");
}

static void answers_an_empty_list_when_the_registry_is_a_directory(void) {
    registry_case open = registry_case_open();
    support_make_directory(open.file);
    support_answer answer = support_call(forebay_library_apps_list, "{}", registry_context(&open));
    expect_json(&answer, 0, "{\"apps\":[]}", "directory registry");
    registry_case_close(&open);
}

static void skips_invalid_entries(void) {
    support_answer answer = list_text(
        "{\"alpha\":" ALPHA ",\"bad\":{\"id\":\"bad\",\"home\":{\"candidates\":[]}},"
        "\"nolabel\":{\"home\":{\"candidates\":[\"/x\"]}},"
        "\"numberlabel\":{\"label\":5,\"home\":{\"candidates\":[\"/x\"]}},"
        "\"emptyid\":{\"id\":\"\",\"label\":\"E\",\"home\":{\"candidates\":[\"/x\"]}},"
        "\"numberid\":{\"id\":7,\"label\":\"N\",\"home\":{\"candidates\":[\"/x\"]}},"
        "\"nohome\":{\"label\":\"H\"},"
        "\"stringhome\":{\"label\":\"H\",\"home\":\"/x\"},"
        "\"candidatesobject\":{\"label\":\"H\",\"home\":{\"candidates\":{\"0\":\"/x\"}}},"
        "\"string\":\"text\",\"number\":4,\"array\":[1],\"true\":true}");
    expect_ids(&answer, "alpha", "invalid entries skipped");
}

static void keeps_an_entry_whose_candidates_are_not_strings(void) {
    support_answer answer = list_text("{\"odd\":{\"label\":\"Odd\",\"home\":{\"candidates\":[5,null]}}}");
    expect_at(&answer, "apps.0.home.candidates", "[5,null]", "raw candidates");
    support_discard(&answer);
}

static void fails_on_a_null_entry_as_basekit_throws(void) {
    support_answer answer = list_text("{\"alpha\":" ALPHA ",\"gone\":null}");
    expect_failure(&answer, "null entry");
}

static void reads_the_registry_afresh_at_every_call(void) {
    registry_case open = registry_case_open();
    support_write(open.file, "{\"alpha\":" ALPHA "}");
    support_answer first = support_call(forebay_library_apps_list, "{}", registry_context(&open));
    expect_ids(&first, "alpha", "first read");
    support_write(open.file, "{\"alpha\":" ALPHA ",\"beta\":" BETA "}");
    support_answer second = support_call(forebay_library_apps_list, "{}", registry_context(&open));
    expect_ids(&second, "alpha,beta", "second read");
    registry_case_close(&open);
}

static void reads_the_ecosystem_registry_under_the_home_without_an_override(void) {
    char *home = support_scratch();
    char *directory = support_join3(home, ".config", "forebay");
    support_make_directories(directory);
    char *file = support_join(directory, "apps.json");
    support_write(file, "{\"beta\":" BETA "}");
    support_answer answer = support_call(forebay_library_apps_list, "{}", support_context(home, NULL));
    expect_ids(&answer, "beta", "ecosystem registry");
    free(file);
    free(directory);
    free(home);
}

static void prefers_the_override_even_when_it_is_absent(void) {
    char *home = support_scratch();
    char *directory = support_join3(home, ".config", "forebay");
    support_make_directories(directory);
    char *file = support_join(directory, "apps.json");
    support_write(file, "{\"beta\":" BETA "}");
    char *missing = support_join(home, "missing.json");
    support_answer answer = support_call(forebay_library_apps_list, "{}", support_context(home, "HUB_APPS_FILE", missing, NULL));
    expect_json(&answer, 0, "{\"apps\":[]}", "absent override wins");
    char *padded = malloc(strlen(file) + 5);
    sprintf(padded, "  %s  ", file);
    support_answer trimmed = support_call(forebay_library_apps_list, "{}", support_context(home, "HUB_APPS_FILE", padded, NULL));
    expect_ids(&trimmed, "beta", "override is trimmed");
    support_answer blank = support_call(forebay_library_apps_list, "{}", support_context(home, "HUB_APPS_FILE", " \t ", NULL));
    expect_ids(&blank, "beta", "a blank override is no override");
    free(padded);
    free(missing);
    free(file);
    free(directory);
    free(home);
}

static void reads_the_legacy_registry_when_the_ecosystem_one_is_absent(void) {
    char *home = support_scratch();
    char *legacy = support_join3(home, ".config", "cairn");
    support_make_directories(legacy);
    char *legacy_file = support_join(legacy, "apps.json");
    support_write(legacy_file, "{\"alpha\":" ALPHA "}");
    support_answer answer = support_call(forebay_library_apps_get, "{\"id\":\"alpha\"}", support_context(home, NULL));
    expect_at(&answer, "app.label", "\"Alpha CLI\"", "legacy registry");
    support_discard(&answer);

    char *canonical = support_join3(home, ".config", "forebay");
    support_make_directories(canonical);
    char *canonical_file = support_join(canonical, "apps.json");
    support_write(canonical_file, "{\"beta\":" BETA "}");
    support_answer preferred = support_call(forebay_library_apps_list, "{}", support_context(home, NULL));
    expect_ids(&preferred, "beta", "the ecosystem registry wins once present");
    free(canonical_file);
    free(canonical);
    free(legacy_file);
    free(legacy);
    free(home);
}

static void gets_one_app_and_nothing_for_an_unknown_id(void) {
    registry_case open = registry_case_open();
    support_write(open.file, "{\"alpha\":" ALPHA ",\"beta\":" BETA "}");
    support_answer known = support_call(forebay_library_apps_get, "{\"id\":\"beta\"}", registry_context(&open));
    expect_at(&known, "app.commandsSubdir", "\"command\"", "known id");
    support_discard(&known);
    support_answer unknown = support_call(forebay_library_apps_get, "{\"id\":\"zeta\"}", registry_context(&open));
    expect_json(&unknown, 0, "{}", "unknown id");
    registry_case_close(&open);
}

static void gets_the_first_of_two_entries_sharing_an_id(void) {
    registry_case open = registry_case_open();
    support_write(open.file,
                  "{\"one\":{\"id\":\"same\",\"label\":\"First\",\"home\":{\"candidates\":[\"/a\"]}},"
                  "\"two\":{\"id\":\"same\",\"label\":\"Second\",\"home\":{\"candidates\":[\"/b\"]}}}");
    support_answer answer = support_call(forebay_library_apps_get, "{\"id\":\"same\"}", registry_context(&open));
    expect_at(&answer, "app.label", "\"First\"", "first of a shared id");
    support_discard(&answer);
    registry_case_close(&open);
}

int main(void) {
    answers_an_empty_list_when_the_registry_is_absent();
    answers_an_empty_list_when_the_registry_is_empty();
    reads_descriptors_solely_from_the_registry();
    resolves_a_whole_descriptor_with_its_defaults();
    defaults_an_entry_id_from_its_key();
    defaults_the_binary_and_the_other_optional_fields();
    answers_an_empty_list_on_a_malformed_registry();
    answers_an_empty_list_when_the_registry_is_not_an_object();
    answers_an_empty_list_when_the_registry_is_a_directory();
    skips_invalid_entries();
    keeps_an_entry_whose_candidates_are_not_strings();
    fails_on_a_null_entry_as_basekit_throws();
    reads_the_registry_afresh_at_every_call();
    reads_the_ecosystem_registry_under_the_home_without_an_override();
    prefers_the_override_even_when_it_is_absent();
    reads_the_legacy_registry_when_the_ecosystem_one_is_absent();
    gets_one_app_and_nothing_for_an_unknown_id();
    gets_the_first_of_two_entries_sharing_an_id();
    return support_summary("list_test");
}
