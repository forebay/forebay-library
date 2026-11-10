#include "fixtures.h"

#define MINIMAL_HOME "\"home\":{\"candidates\":[\"/m\"]}"

static void orders_entries_as_object_entries_does(void) {
    support_answer answer = list_text(
        "{\"b\":{\"label\":\"B\"," MINIMAL_HOME "},\"10\":{\"label\":\"T\"," MINIMAL_HOME "},"
        "\"01\":{\"label\":\"Z\"," MINIMAL_HOME "},\"2\":{\"label\":\"W\"," MINIMAL_HOME "},"
        "\"-1\":{\"label\":\"N\"," MINIMAL_HOME "},\"4294967295\":{\"label\":\"L\"," MINIMAL_HOME "},"
        "\"4294967294\":{\"label\":\"M\"," MINIMAL_HOME "},\"0\":{\"label\":\"O\"," MINIMAL_HOME "},"
        "\"a\":{\"label\":\"A\"," MINIMAL_HOME "}}");
    expect_ids(&answer, "0,2,10,4294967294,b,01,-1,4294967295,a", "integer-like keys first, ascending");
}

static void keeps_the_last_value_of_a_duplicated_key_at_its_first_position(void) {
    support_answer answer = list_text(
        "{\"x\":{\"label\":\"First\"," MINIMAL_HOME "},\"y\":{\"label\":\"Y\"," MINIMAL_HOME "},"
        "\"x\":{\"label\":\"Last\"," MINIMAL_HOME "}}");
    expect_at(&answer, "apps.0.label", "\"Last\"", "last value of a duplicate");
    expect_ids(&answer, "x,y", "first position of a duplicate");
}

static void takes_the_default_for_a_null_field_as_for_an_absent_one(void) {
    support_answer answer = list_text(
        "{\"key\":{\"id\":null,\"label\":\"L\"," MINIMAL_HOME ",\"detect\":{\"binary\":null,\"pkg\":null},"
        "\"commandsSubdir\":null,\"proxyPort\":null,\"integration\":null,\"wireFormat\":null,\"paths\":null}}");
    expect_json(&answer, 0,
                "{\"apps\":[{\"id\":\"key\",\"label\":\"L\"," MINIMAL_HOME ",\"detect\":{\"binary\":\"key\","
                "\"pkg\":\"\"},\"commandsSubdir\":\"commands\",\"paths\":{\"repos\":\"repos\",\"plugin\":"
                "\"plugin\",\"cache\":\"cache\",\"config\":\"config\"},\"proxyPort\":0,\"integration\":"
                "\"env-baseurl\",\"wireFormat\":\"anthropic\"}]}",
                "null takes the default");
}

static void takes_the_id_for_a_detect_that_is_not_an_object(void) {
    support_answer answer = list_text("{\"k\":{\"label\":\"L\"," MINIMAL_HOME ",\"detect\":\"binary\"}}");
    expect_at(&answer, "apps.0.detect", "{\"binary\":\"k\",\"pkg\":\"\"}", "string detect");
    support_discard(&answer);
}

static void copies_a_declared_value_whatever_its_type(void) {
    support_answer answer = list_text(
        "{\"k\":{\"label\":\"L\"," MINIMAL_HOME ",\"detect\":{\"binary\":7,\"pkg\":false},\"commandsSubdir\":3,"
        "\"proxyPort\":\"80\",\"integration\":[],\"wireFormat\":{}}}");
    expect_at(&answer, "apps.0.detect", "{\"binary\":7,\"pkg\":false}", "raw detect");
    expect_at(&answer, "apps.0.commandsSubdir", "3", "raw commandsSubdir");
    expect_at(&answer, "apps.0.proxyPort", "\"80\"", "raw proxyPort");
    expect_at(&answer, "apps.0.integration", "[]", "raw integration");
    expect_at(&answer, "apps.0.wireFormat", "{}", "raw wireFormat");
    support_discard(&answer);
}

static void keeps_a_null_optional_field_and_omits_an_absent_one(void) {
    support_answer answer = list_text(
        "{\"k\":{\"label\":\"L\"," MINIMAL_HOME ",\"icon\":null,\"loader\":null,\"usage\":null,\"accent\":null,"
        "\"wrapperCommand\":null,\"npmPlugins\":null,\"startupHook\":null,\"discovery\":null,\"projects\":null,"
        "\"modelCatalog\":null,\"unknown\":1}}");
    expect_json(&answer, 0,
                "{\"apps\":[{\"id\":\"k\",\"label\":\"L\",\"icon\":null," MINIMAL_HOME ",\"detect\":{\"binary\":"
                "\"k\",\"pkg\":\"\"},\"loader\":null,\"commandsSubdir\":\"commands\",\"paths\":{\"repos\":"
                "\"repos\",\"plugin\":\"plugin\",\"cache\":\"cache\",\"config\":\"config\"},\"proxyPort\":0,"
                "\"integration\":\"env-baseurl\",\"wireFormat\":\"anthropic\",\"usage\":null,\"accent\":null,"
                "\"wrapperCommand\":null,\"npmPlugins\":null,\"startupHook\":null,\"discovery\":null,"
                "\"projects\":null,\"modelCatalog\":null}]}",
                "null optional fields survive, unknown ones do not");
}

static void copies_every_optional_field(void) {
    support_answer answer = list_text(
        "{\"k\":{\"label\":\"L\"," MINIMAL_HOME ",\"icon\":\"<svg/>\",\"accent\":\"#112233\",\"wrapperCommand\":"
        "\"kk\",\"npmPlugins\":{\"configFiles\":[\"a\"],\"pluginsKey\":\"p\"},\"startupHook\":{\"file\":\"f\","
        "\"path\":[\"x\"],\"entry\":{}},\"discovery\":{\"topic\":\"t\"},\"projects\":{\"historyFile\":\"h\"},"
        "\"modelCatalog\":{\"files\":[\"m\"],\"providerKey\":\"pk\"}}}");
    expect_at(&answer, "apps.0.icon", "\"<svg/>\"", "icon");
    expect_at(&answer, "apps.0.accent", "\"#112233\"", "accent");
    expect_at(&answer, "apps.0.wrapperCommand", "\"kk\"", "wrapperCommand");
    expect_at(&answer, "apps.0.npmPlugins", "{\"configFiles\":[\"a\"],\"pluginsKey\":\"p\"}", "npmPlugins");
    expect_at(&answer, "apps.0.startupHook", "{\"file\":\"f\",\"path\":[\"x\"],\"entry\":{}}", "startupHook");
    expect_at(&answer, "apps.0.discovery", "{\"topic\":\"t\"}", "discovery");
    expect_at(&answer, "apps.0.projects", "{\"historyFile\":\"h\"}", "projects");
    expect_at(&answer, "apps.0.modelCatalog", "{\"files\":[\"m\"],\"providerKey\":\"pk\"}", "modelCatalog");
    support_discard(&answer);
}

static void takes_a_declared_path_name_trimmed(void) {
    support_answer answer = list_text(
        "{\"k\":{\"label\":\"L\"," MINIMAL_HOME ",\"paths\":{\"repos\":\"  checkouts \",\"plugin\":\"p\","
        "\"cache\":\"\\u00a0c\\ufeff\",\"config\":\"cfg\"}}}");
    expect_at(&answer, "apps.0.paths", "{\"repos\":\"checkouts\",\"plugin\":\"p\",\"cache\":\"c\",\"config\":\"cfg\"}",
              "trimmed names");
    support_discard(&answer);
}

static void refuses_a_path_name_that_is_not_one_segment(void) {
    support_answer answer = list_text(
        "{\"k\":{\"label\":\"L\"," MINIMAL_HOME ",\"paths\":{\"repos\":\".\",\"plugin\":\"..\",\"cache\":\"a/b\","
        "\"config\":\"a\\\\b\"}},\"j\":{\"label\":\"J\"," MINIMAL_HOME ",\"paths\":{\"repos\":\"   \","
        "\"plugin\":5,\"cache\":null,\"config\":\"\"}},\"s\":{\"label\":\"S\"," MINIMAL_HOME ",\"paths\":\"x\"}}");
    const char *defaults = "{\"repos\":\"repos\",\"plugin\":\"plugin\",\"cache\":\"cache\",\"config\":\"config\"}";
    expect_at(&answer, "apps.0.paths", defaults, "separators and dots refused");
    expect_at(&answer, "apps.1.paths", defaults, "blank, non-string and null refused");
    expect_at(&answer, "apps.2.paths", defaults, "a paths that is not an object");
    support_discard(&answer);
}

static void takes_a_path_name_from_the_environment_after_the_declared_one(void) {
    registry_case open = registry_case_open();
    support_write(open.file, "{\"k\":{\"label\":\"L\"," MINIMAL_HOME ",\"paths\":{\"repos\":\"mine\",\"cache\":\"..\"}}}");
    cJSON *context = support_context(open.home, "HUB_APPS_FILE", open.file, "HUB_REPOS_SUBDIR", "env-repos",
                                     "HUB_PLUGIN_SUBDIR", " env-plugin ", "HUB_CACHE_SUBDIR", "env-cache",
                                     "HUB_CONFIG_SUBDIR", "bad/name", NULL);
    support_answer answer = support_call(forebay_library_apps_list, "{}", context);
    expect_at(&answer, "apps.0.paths",
              "{\"repos\":\"mine\",\"plugin\":\"env-plugin\",\"cache\":\"env-cache\",\"config\":\"config\"}",
              "environment names");
    support_discard(&answer);
    registry_case_close(&open);
}

int main(void) {
    orders_entries_as_object_entries_does();
    keeps_the_last_value_of_a_duplicated_key_at_its_first_position();
    takes_the_default_for_a_null_field_as_for_an_absent_one();
    takes_the_id_for_a_detect_that_is_not_an_object();
    copies_a_declared_value_whatever_its_type();
    keeps_a_null_optional_field_and_omits_an_absent_one();
    copies_every_optional_field();
    takes_a_declared_path_name_trimmed();
    refuses_a_path_name_that_is_not_one_segment();
    takes_a_path_name_from_the_environment_after_the_declared_one();
    return support_summary("descriptor_test");
}
