#include "support.h"

int32_t forebay_library_accounts_list(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);
int32_t forebay_library_accounts_upsert(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);
int32_t forebay_library_accounts_remove(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);
int32_t forebay_library_accounts_acquire(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);
int32_t forebay_library_accounts_report(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);
int32_t forebay_library_accounts_token_expired(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);
int32_t forebay_library_accounts_has_capacity(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);

typedef struct named_function {
    const char *name;
    apps_function function;
    int takes_store;
} named_function;

static const named_function functions[] = {
    {"list", forebay_library_accounts_list, 1},
    {"upsert", forebay_library_accounts_upsert, 1},
    {"remove", forebay_library_accounts_remove, 1},
    {"acquire", forebay_library_accounts_acquire, 1},
    {"report", forebay_library_accounts_report, 1},
    {"tokenExpired", forebay_library_accounts_token_expired, 0},
    {"hasCapacity", forebay_library_accounts_has_capacity, 0},
};

static const named_function *find_function(const char *name) {
    for (size_t index = 0; index < sizeof(functions) / sizeof(functions[0]); index++) {
        if (strcmp(functions[index].name, name) == 0) {
            return &functions[index];
        }
    }
    return NULL;
}

static int file_exists(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file != NULL) {
        fclose(file);
    }
    return file != NULL;
}

static void lay_out_store(const cJSON *vector, const char *store) {
    const cJSON *before = cJSON_GetObjectItemCaseSensitive(vector, "before");
    const cJSON *before_text = cJSON_GetObjectItemCaseSensitive(vector, "beforeText");
    if (cJSON_IsObject(before)) {
        char *text = cJSON_PrintUnformatted(before);
        support_write(store, text);
        free(text);
    } else if (cJSON_IsString(before_text)) {
        support_write(store, before_text->valuestring);
    }
}

static void expect_store(const cJSON *step, const char *store, const char *label) {
    const cJSON *after = cJSON_GetObjectItemCaseSensitive(step, "after");
    const cJSON *after_text = cJSON_GetObjectItemCaseSensitive(step, "afterText");
    char *text = support_read_text(store);
    if (cJSON_IsNull(after)) {
        if (text != NULL) {
            support_failf(label, "the store should not exist, it holds %s", text);
        }
    } else if (cJSON_IsObject(after)) {
        cJSON *stored = text == NULL ? NULL : cJSON_Parse(text);
        if (stored == NULL || !cJSON_Compare(stored, after, 1)) {
            char *wanted = cJSON_PrintUnformatted(after);
            support_failf(label, "the store differs\n  expected %s\n  actual   %s", wanted,
                          text == NULL ? "(absent)" : text);
            free(wanted);
        }
        cJSON_Delete(stored);
    } else if (cJSON_IsString(after_text) && (text == NULL || strcmp(text, after_text->valuestring) != 0)) {
        support_failf(label, "the store text differs: %s", text == NULL ? "(absent)" : text);
    }
    free(text);
}

static void run_step(const cJSON *step, const char *store, const char *path) {
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(step, "function");
    const cJSON *argument = cJSON_GetObjectItemCaseSensitive(step, "argument");
    const cJSON *result = cJSON_GetObjectItemCaseSensitive(step, "result");
    const named_function *target = cJSON_IsString(name) ? find_function(name->valuestring) : NULL;
    if (target == NULL || argument == NULL || result == NULL) {
        support_fail(path, "not a vector step: needs a known function, an argument and a result");
        return;
    }
    char label[1100];
    snprintf(label, sizeof(label), "%s [%s]", path, name->valuestring);
    cJSON *call = cJSON_Duplicate(argument, 1);
    if (target->takes_store) {
        cJSON_AddStringToObject(call, "store", store);
    }
    char *input = cJSON_PrintUnformatted(call);
    char *expected = cJSON_PrintUnformatted(result);
    support_answer answer = support_call_raw(target->function, input);
    expect_json(&answer, 0, expected, label);
    expect_store(step, store, label);
    free(input);
    free(expected);
    cJSON_Delete(call);
}

static void run_vector(const char *path) {
    char *text = support_read_text(path);
    cJSON *vector = text == NULL ? NULL : cJSON_Parse(text);
    free(text);
    if (vector == NULL) {
        support_fail(path, "not JSON");
        return;
    }
    char *directory = support_scratch();
    char *store = support_join(directory, "accounts.json");
    lay_out_store(vector, store);
    run_step(vector, store, path);
    const cJSON *step = NULL;
    cJSON_ArrayForEach(step, cJSON_GetObjectItemCaseSensitive(vector, "then")) {
        run_step(step, store, path);
    }
    free(store);
    free(directory);
    cJSON_Delete(vector);
}

static int run_every_vector(void) {
    char *list = support_read_text(ACCOUNTS_VECTOR_LIST);
    if (list == NULL) {
        support_fail("vector list", ACCOUNTS_VECTOR_LIST);
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

/** Calls function with members_json's members, plus store when one is given. */
static support_answer call_with(apps_function function, const char *store, const char *members_json) {
    cJSON *call = cJSON_Parse(members_json);
    if (store != NULL) {
        cJSON_AddStringToObject(call, "store", store);
    }
    char *input = cJSON_PrintUnformatted(call);
    support_answer answer = support_call_raw(function, input);
    free(input);
    cJSON_Delete(call);
    return answer;
}

typedef struct malformed_call {
    const char *label;
    apps_function function;
    int sends_store;
    const char *members;
} malformed_call;

static const malformed_call malformed_calls[] = {
    {"list without a store", forebay_library_accounts_list, 0, "{}"},
    {"list with a store that is a number", forebay_library_accounts_list, 0, "{\"store\":5}"},
    {"list with an empty store", forebay_library_accounts_list, 0, "{\"store\":\"\"}"},
    {"upsert without an account", forebay_library_accounts_upsert, 1, "{}"},
    {"upsert of an account that is a list", forebay_library_accounts_upsert, 1, "{\"account\":[]}"},
    {"upsert of an account without an id", forebay_library_accounts_upsert, 1, "{\"account\":{\"label\":\"x\"}}"},
    {"upsert of an account whose id is a number", forebay_library_accounts_upsert, 1, "{\"account\":{\"id\":4}}"},
    {"upsert of an account whose id is empty", forebay_library_accounts_upsert, 1, "{\"account\":{\"id\":\"\"}}"},
    {"remove without an id", forebay_library_accounts_remove, 1, "{}"},
    {"remove with an id that is a number", forebay_library_accounts_remove, 1, "{\"id\":3}"},
    {"acquire without a strategy", forebay_library_accounts_acquire, 1, "{\"now\":5}"},
    {"acquire with a strategy that is a number", forebay_library_accounts_acquire, 1, "{\"strategy\":1,\"now\":5}"},
    {"acquire without now", forebay_library_accounts_acquire, 1, "{\"strategy\":\"hybrid\"}"},
    {"acquire with now as text", forebay_library_accounts_acquire, 1, "{\"strategy\":\"hybrid\",\"now\":\"5\"}"},
    {"acquire with a lane that is a number", forebay_library_accounts_acquire, 1, "{\"strategy\":\"hybrid\",\"now\":5,\"lane\":2}"},
    {"report without an id", forebay_library_accounts_report, 1, "{\"outcome\":\"success\",\"now\":5}"},
    {"report without an outcome", forebay_library_accounts_report, 1, "{\"id\":\"a\",\"now\":5}"},
    {"report with an unknown outcome", forebay_library_accounts_report, 1, "{\"id\":\"a\",\"outcome\":\"explode\",\"now\":5}"},
    {"report without now", forebay_library_accounts_report, 1, "{\"id\":\"a\",\"outcome\":\"success\"}"},
    {"report with a lane that is a number", forebay_library_accounts_report, 1,
     "{\"id\":\"a\",\"outcome\":\"success\",\"now\":5,\"lane\":1}"},
    {"report with a reason that is a number", forebay_library_accounts_report, 1,
     "{\"id\":\"a\",\"outcome\":\"disabled\",\"now\":5,\"reason\":1}"},
    {"report error with random 1", forebay_library_accounts_report, 1,
     "{\"id\":\"a\",\"outcome\":\"error\",\"now\":5,\"random\":1,\"baseMilliseconds\":1,\"maxMilliseconds\":2}"},
    {"report error with a negative random", forebay_library_accounts_report, 1,
     "{\"id\":\"a\",\"outcome\":\"error\",\"now\":5,\"random\":-0.1,\"baseMilliseconds\":1,\"maxMilliseconds\":2}"},
    {"report error with random as text", forebay_library_accounts_report, 1,
     "{\"id\":\"a\",\"outcome\":\"error\",\"now\":5,\"random\":\"0.5\",\"baseMilliseconds\":1,\"maxMilliseconds\":2}"},
    {"report error without a base", forebay_library_accounts_report, 1,
     "{\"id\":\"a\",\"outcome\":\"error\",\"now\":5,\"maxMilliseconds\":2}"},
    {"report error without a maximum", forebay_library_accounts_report, 1,
     "{\"id\":\"a\",\"outcome\":\"error\",\"now\":5,\"baseMilliseconds\":2}"},
    {"report error with a negative base", forebay_library_accounts_report, 1,
     "{\"id\":\"a\",\"outcome\":\"error\",\"now\":5,\"baseMilliseconds\":-1,\"maxMilliseconds\":2}"},
    {"report error with an attempt as text", forebay_library_accounts_report, 1,
     "{\"id\":\"a\",\"outcome\":\"error\",\"now\":5,\"attempt\":\"1\",\"baseMilliseconds\":1,\"maxMilliseconds\":2}"},
    {"report rate-limited with neither a reset nor a backoff", forebay_library_accounts_report, 1,
     "{\"id\":\"a\",\"outcome\":\"rate-limited\",\"now\":5}"},
    {"report rate-limited with a reset as text", forebay_library_accounts_report, 1,
     "{\"id\":\"a\",\"outcome\":\"rate-limited\",\"now\":5,\"resetAt\":\"9\"}"},
    {"report quota without a reading", forebay_library_accounts_report, 1, "{\"id\":\"a\",\"outcome\":\"quota\",\"now\":5}"},
    {"report quota with a reading that is a number", forebay_library_accounts_report, 1,
     "{\"id\":\"a\",\"outcome\":\"quota\",\"now\":5,\"quota\":7}"},
    {"tokenExpired without now", forebay_library_accounts_token_expired, 0, "{\"expires\":5}"},
    {"tokenExpired with expires as text", forebay_library_accounts_token_expired, 0, "{\"expires\":\"5\",\"now\":5}"},
    {"tokenExpired with a skew as text", forebay_library_accounts_token_expired, 0, "{\"expires\":5,\"now\":5,\"skewMilliseconds\":\"1\"}"},
    {"tokenExpired with a negative skew", forebay_library_accounts_token_expired, 0, "{\"expires\":5,\"now\":5,\"skewMilliseconds\":-1}"},
    {"hasCapacity without pools", forebay_library_accounts_has_capacity, 0, "{}"},
    {"hasCapacity with pools as an object", forebay_library_accounts_has_capacity, 0, "{\"pools\":{}}"},
};

static void refuses_a_malformed_call(void) {
    char *directory = support_scratch();
    char *store = support_join(directory, "accounts.json");
    for (size_t index = 0; index < sizeof(malformed_calls) / sizeof(malformed_calls[0]); index++) {
        const malformed_call *call = &malformed_calls[index];
        support_answer answer = call_with(call->function, call->sends_store ? store : NULL, call->members);
        expect_failure(&answer, call->label);
    }
    if (file_exists(store)) {
        support_fail("malformed calls", "a malformed call created the store");
    }
    support_answer not_object = support_call_raw(forebay_library_accounts_list, "[]");
    expect_failure(&not_object, "list with an array argument");
    support_answer not_json = support_call_raw(forebay_library_accounts_list, "{\"store\":");
    expect_failure(&not_json, "list with broken json");
    free(store);
    free(directory);
}

static void creates_the_missing_parent_directories_and_the_lock_file(void) {
    char *directory = support_scratch();
    char *inner = support_join3(directory, "one", "two");
    char *store = support_join(inner, "accounts.json");
    char *lock = support_join(inner, "accounts.json.lock");
    support_answer answer = call_with(forebay_library_accounts_upsert, store, "{\"account\":{\"id\":\"acc1\",\"label\":\"acc1\"}}");
    expect_json(&answer, 0, "{\"outcome\":\"added\"}", "upsert into missing directories");
    if (!file_exists(store)) {
        support_fail("missing directories", "the store was not created");
    }
    if (!file_exists(lock)) {
        support_fail("missing directories", "the lock file <store>.lock was not created");
    }
    free(lock);
    free(store);
    free(inner);
    free(directory);
}

static const char *const token_names[] = {"access", "refresh", "accessToken", "refreshToken", "idToken", "apiKey"};

static void token_members_are_refused(void) {
    char *directory = support_scratch();
    char *store = support_join(directory, "accounts.json");
    for (size_t index = 0; index < sizeof(token_names) / sizeof(token_names[0]); index++) {
        char members[256];
        snprintf(members, sizeof(members), "{\"account\":{\"id\":\"acc1\",\"%s\":\"secret-value\"}}", token_names[index]);
        support_answer answer = call_with(forebay_library_accounts_upsert, store, members);
        const cJSON *refused = support_at(answer.result, "refused");
        if (answer.status != 0 || !cJSON_IsString(refused) || strstr(refused->valuestring, token_names[index]) == NULL) {
            support_failf(token_names[index], "expected a refusal naming the member: %s", answer.text);
        }
        support_discard(&answer);
    }
    if (file_exists(store)) {
        support_fail("token members", "a refused account reached the store");
    }
    free(store);
    free(directory);
}

static void no_operation_writes_a_secret_looking_member(void) {
    char *directory = support_scratch();
    char *store = support_join(directory, "accounts.json");
    support_answer added = call_with(forebay_library_accounts_upsert, store, "{\"account\":{\"id\":\"acc1\",\"label\":\"acc1\",\"expires\":9}}");
    support_discard(&added);
    support_answer claimed = call_with(forebay_library_accounts_acquire, store, "{\"strategy\":\"hybrid\",\"now\":5,\"lane\":\"chat\"}");
    support_discard(&claimed);
    const char *outcomes[] = {"success", "error", "rate-limited", "disabled"};
    for (size_t index = 0; index < 4; index++) {
        char members[256];
        snprintf(members, sizeof(members),
                 "{\"id\":\"acc1\",\"lane\":\"chat\",\"outcome\":\"%s\",\"now\":5,\"resetAt\":9,\"baseMilliseconds\":1,"
                 "\"maxMilliseconds\":2}",
                 outcomes[index]);
        support_answer reported = call_with(forebay_library_accounts_report, store, members);
        support_discard(&reported);
    }
    char *text = support_read_text(store);
    if (text == NULL || strstr(text, "token") != NULL || strstr(text, "access\"") != NULL ||
        strstr(text, "refresh\"") != NULL) {
        support_fail("no secrets", text == NULL ? "the store is missing" : text);
    }
    free(text);
    free(store);
    free(directory);
}

static void whole_numbers_keep_their_text(void) {
    char *directory = support_scratch();
    char *store = support_join(directory, "accounts.json");
    support_answer answer = call_with(
        forebay_library_accounts_upsert, store,
        "{\"account\":{\"id\":\"acc-meta\",\"meta\":{\"count\":5,\"remainingFraction\":0.5}}}");
    support_discard(&answer);
    char *text = support_read_text(store);
    if (text == NULL || strstr(text, "\"count\": 5") == NULL || strstr(text, "\"count\": 5.0") != NULL ||
        strstr(text, "\"remainingFraction\": 0.5") == NULL) {
        support_fail("meta numbers", text == NULL ? "the store is missing" : text);
    }
    free(text);
    free(store);
    free(directory);
}

int main(void) {
    int vectors = run_every_vector();
    if (vectors == 0) {
        support_fail("vectors", "no accounts vector was found");
    }
    refuses_a_malformed_call();
    creates_the_missing_parent_directories_and_the_lock_file();
    token_members_are_refused();
    no_operation_writes_a_secret_looking_member();
    whole_numbers_keep_their_text();
    printf("accounts_test: %d vectors\n", vectors);
    return support_summary("accounts_test");
}
