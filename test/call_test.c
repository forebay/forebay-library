#include "fixtures.h"

static void refuses_an_id_that_is_not_a_string(void) {
    registry_case open = registry_case_open();
    support_answer missing = support_call(forebay_library_apps_get, "{}", registry_context(&open));
    expect_failure(&missing, "get without an id");
    support_answer number = support_call(forebay_library_apps_get, "{\"id\":5}", registry_context(&open));
    expect_failure(&number, "get with a number id");
    support_answer home = support_call(forebay_library_apps_home, "{\"id\":null}", registry_context(&open));
    expect_failure(&home, "home with a null id");
    support_answer array = support_call(forebay_library_apps_home, "[\"alpha\"]", registry_context(&open));
    expect_failure(&array, "home with an array argument");
    registry_case_close(&open);
}

static void refuses_an_argument_that_is_not_an_object(void) {
    registry_case open = registry_case_open();
    support_answer list = support_call(forebay_library_apps_list, "[]", registry_context(&open));
    expect_failure(&list, "list with an array argument");
    cJSON *context = registry_context(&open);
    support_arguments(context, NULL);
    support_answer current = support_call(forebay_library_apps_current, "\"x\"", context);
    expect_failure(&current, "current with a string argument");
    registry_case_close(&open);
}

static void refuses_a_context_missing_a_fact_the_function_reads(void) {
    support_answer no_home = support_call_raw(forebay_library_apps_list, "{\"argument\":{},\"context\":{\"environment\":{}}}");
    expect_failure(&no_home, "no home");
    support_answer no_environment = support_call_raw(forebay_library_apps_get, "{\"argument\":{\"id\":\"a\"},\"context\":{\"home\":\"/h\"}}");
    expect_failure(&no_environment, "no environment");
    support_answer no_arguments =
        support_call_raw(forebay_library_apps_current, "{\"argument\":{},\"context\":{\"home\":\"/h\",\"environment\":{}}}");
    expect_failure(&no_arguments, "no arguments");
}

static void refuses_an_input_that_is_not_an_envelope(void) {
    support_answer plain = support_call_raw(forebay_library_apps_list, "{}");
    expect_failure(&plain, "no envelope");
    support_answer broken = support_call_raw(forebay_library_apps_list, "{\"argument\":");
    expect_failure(&broken, "not json");
    support_answer bytes = support_call_raw(forebay_library_apps_list, "{\"argument\":{},\"context\":{\"home\":\"\xFF\"}}");
    expect_failure(&bytes, "not utf-8");
}

static void answers_with_a_buffer_the_release_function_frees(void) {
    char *home = support_scratch();
    cJSON *context = support_context(home, NULL);
    cJSON *envelope = cJSON_CreateObject();
    cJSON_AddItemToObject(envelope, "argument", cJSON_CreateObject());
    cJSON_AddItemToObject(envelope, "context", context);
    char *input = cJSON_PrintUnformatted(envelope);
    uint8_t *out = NULL;
    size_t out_len = 0;
    int32_t status = forebay_library_apps_list((const uint8_t *)input, strlen(input), &out, &out_len);
    if (status != 0 || out == NULL || out_len != strlen("{\"apps\":[]}") || memcmp(out, "{\"apps\":[]}", out_len) != 0) {
        support_fail("release", "expected exactly {\"apps\":[]}");
    }
    forebay_library_release(out);
    forebay_library_release(NULL);
    free(input);
    cJSON_Delete(envelope);
    free(home);
}

int main(void) {
    refuses_an_id_that_is_not_a_string();
    refuses_an_argument_that_is_not_an_object();
    refuses_a_context_missing_a_fact_the_function_reads();
    refuses_an_input_that_is_not_an_envelope();
    answers_with_a_buffer_the_release_function_frees();
    return support_summary("call_test");
}
