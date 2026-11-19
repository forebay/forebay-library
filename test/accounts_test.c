#include "support.h"

#include <math.h>

#include "accounts_call.h"
#include "spisor/json_allocation.h"
#include "spisor/allocation.h"

/**
 * @file
 * Every accounts vector, each step called through the definition and its answer compared with the result the vector
 * writes, then the store with the one it writes after; and the calls no vector can write: malformed structs, answers
 * that are not empty, and what the store looks like on disk.
 */

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
        cJSON_free(text);
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
            cJSON_free(wanted);
        }
        cJSON_Delete(stored);
    } else if (cJSON_IsString(after_text) && (text == NULL || strcmp(text, after_text->valuestring) != 0)) {
        support_failf(label, "the store text differs: %s", text == NULL ? "(absent)" : text);
    }
    free(text);
}

static void expect_result(const cJSON *expected, const accounts_call *call, const accounts_answers *answers,
                          const char *label) {
    cJSON *actual = accounts_result_of(call, answers);
    if (!cJSON_Compare(actual, expected, 1)) {
        char *wanted = cJSON_PrintUnformatted(expected);
        char *got = cJSON_PrintUnformatted(actual);
        support_failf(label, "status %d\n  expected %s\n  actual   %s", (int)answers->status, wanted, got);
        cJSON_free(wanted);
        cJSON_free(got);
    }
    cJSON_Delete(actual);
}

static void run_step(const cJSON *step, const char *store, const char *path) {
    const cJSON *result = cJSON_GetObjectItemCaseSensitive(step, "result");
    accounts_call call;
    accounts_answers answers;
    char label[1100];
    snprintf(label, sizeof(label), "%s [%s]", path, accounts_optional_text(step, "function"));
    if (result == NULL || !accounts_read_call(step, store, &call)) {
        support_fail(label, "not a vector step: needs a known function, an argument and a result");
        return;
    }
    accounts_make_call(&call, &answers);
    expect_result(result, &call, &answers, label);
    expect_store(step, store, label);
    accounts_release_answers(&answers);
    accounts_release_call(&call);
}

static void run_vector(const char *path) {
    cJSON *vector = spisor_vector_read(path);
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
    spisor_memory_release(store);
    spisor_memory_release(directory);
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

static void expect_status(spisor_status status, spisor_status expected, spisor_refusal *refusal, const char *label) {
    if (status != expected || (expected != SPISOR_OK && (refusal->detail == NULL || refusal->fix == NULL))) {
        support_failf(label, "expected status %d with a refusal, got %d refusing with \"%s\"", (int)expected,
                      (int)status, refusal->detail == NULL ? "(none)" : refusal->detail);
    }
    if (refusal->fix != NULL && (strstr(refusal->fix, "JSON") != NULL || strstr(refusal->fix, "send ") != NULL)) {
        support_failf(label, "the fix speaks JSON, not the struct: %s", refusal->fix);
    }
    accounts_definition()->release_refusal(NULL, refusal);
}

static spisor_value text_value(const char *text) {
    spisor_value value = {SPISOR_VALUE_TEXT, 0, 0, text, strlen(text), NULL, NULL, 0};
    return value;
}

static spisor_entry id_entry(const char *id) {
    spisor_entry entry = {"id", 2, text_value(id)};
    return entry;
}

static spisor_value record_of(const spisor_entry *entries, size_t count) {
    spisor_value record = {SPISOR_VALUE_RECORD, 0, 0, NULL, 0, NULL, entries, count};
    return record;
}

static void malformed_list_and_upsert(const char *store) {
    const forebay_accounts_definition *accounts = accounts_definition();
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    forebay_accounts_list_argument list = SPISOR_INIT(forebay_accounts_list_argument);
    forebay_accounts_list_answer listed = SPISOR_INIT(forebay_accounts_list_answer);
    forebay_accounts_upsert_argument upsert = SPISOR_INIT(forebay_accounts_upsert_argument);
    forebay_accounts_upsert_answer upserted = SPISOR_INIT(forebay_accounts_upsert_answer);
    spisor_entry entries[1] = {id_entry("")};
    spisor_entry broken[1] = {id_entry("\xff")};
    spisor_value account = record_of(entries, 1);
    expect_status(accounts->list(NULL, &list, &listed, &refusal), SPISOR_MALFORMED, &refusal, "list without a store");
    list.store = "";
    expect_status(accounts->list(NULL, &list, &listed, &refusal), SPISOR_MALFORMED, &refusal, "list, empty store");
    list.store = store;
    list.size = 4;
    expect_status(accounts->list(NULL, &list, &listed, &refusal), SPISOR_MALFORMED, &refusal, "list, short argument");
    expect_status(accounts->list(NULL, NULL, &listed, &refusal), SPISOR_MALFORMED, &refusal, "list, no argument");
    list.size = sizeof list;
    expect_status(accounts->list(NULL, &list, NULL, &refusal), SPISOR_MALFORMED, &refusal, "list, no answer");
    listed.count = 3;
    expect_status(accounts->list(NULL, &list, &listed, &refusal), SPISOR_MALFORMED, &refusal,
                  "list into an answer whose count is not zero beside no storage");
    upsert.store = store;
    expect_status(accounts->upsert(NULL, &upsert, &upserted, &refusal), SPISOR_MALFORMED, &refusal,
                  "upsert without an account");
    upsert.account = &account;
    expect_status(accounts->upsert(NULL, &upsert, &upserted, &refusal), SPISOR_MALFORMED, &refusal,
                  "upsert of an account whose id is empty");
    account = record_of(broken, 1);
    expect_status(accounts->upsert(NULL, &upsert, &upserted, &refusal), SPISOR_MALFORMED, &refusal,
                  "upsert of an account whose id is not UTF-8");
    account = text_value("acc1");
    expect_status(accounts->upsert(NULL, &upsert, &upserted, &refusal), SPISOR_MALFORMED, &refusal,
                  "upsert of an account that is text");
}

static void malformed_remove_and_acquire(const char *store) {
    const forebay_accounts_definition *accounts = accounts_definition();
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    forebay_accounts_remove_argument remove = SPISOR_INIT(forebay_accounts_remove_argument);
    forebay_accounts_remove_answer removed = SPISOR_INIT(forebay_accounts_remove_answer);
    forebay_accounts_acquire_argument acquire = SPISOR_INIT(forebay_accounts_acquire_argument);
    forebay_accounts_acquire_answer acquired = SPISOR_INIT(forebay_accounts_acquire_answer);
    char held = 0;
    remove.store = store;
    expect_status(accounts->remove(NULL, &remove, &removed, &refusal), SPISOR_MALFORMED, &refusal,
                  "remove without an id");
    acquire.store = store;
    acquire.now = (double)INFINITY;
    expect_status(accounts->acquire(NULL, &acquire, &acquired, &refusal), SPISOR_MALFORMED, &refusal,
                  "acquire at an infinite now");
    acquire.now = 5;
    acquired.storage = &held;
    expect_status(accounts->acquire(NULL, &acquire, &acquired, &refusal), SPISOR_MALFORMED, &refusal,
                  "acquire into an answer holding storage");
}

static void expect_report_refused(forebay_accounts_report_argument *report, const char *label) {
    const forebay_accounts_definition *accounts = accounts_definition();
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    forebay_accounts_report_answer reported = SPISOR_INIT(forebay_accounts_report_answer);
    expect_status(accounts->report(NULL, report, &reported, &refusal), SPISOR_MALFORMED, &refusal, label);
}

static forebay_accounts_report_argument error_report(const char *store) {
    forebay_accounts_report_argument report = SPISOR_INIT(forebay_accounts_report_argument);
    report.store = store;
    report.id = "a";
    report.outcome = FOREBAY_ACCOUNTS_ERROR;
    report.now = 5;
    report.base_milliseconds = 1;
    report.max_milliseconds = 2;
    return report;
}

static void malformed_report(const char *store) {
    forebay_accounts_report_argument report = error_report(store);
    spisor_value number = {SPISOR_VALUE_NUMBER, 0, 7, NULL, 0, NULL, NULL, 0};
    report.id = NULL;
    expect_report_refused(&report, "report without an id");
    report = error_report(store);
    report.outcome = 9;
    expect_report_refused(&report, "report with an unknown outcome");
    report = error_report(store);
    report.has_random = 1;
    report.random = 1;
    expect_report_refused(&report, "report error with random 1");
    report.random = -0.1;
    expect_report_refused(&report, "report error with a negative random");
    report = error_report(store);
    report.base_milliseconds = -1;
    expect_report_refused(&report, "report error with a negative base");
    report = error_report(store);
    report.now = (double)NAN;
    expect_report_refused(&report, "report at a now that is not a number");
    report = error_report(store);
    report.outcome = FOREBAY_ACCOUNTS_QUOTA;
    expect_report_refused(&report, "report quota without a reading");
    report.quota = &number;
    expect_report_refused(&report, "report quota with a reading that is a number");
}

static void malformed_token_and_capacity(void) {
    const forebay_accounts_definition *accounts = accounts_definition();
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    forebay_accounts_token_argument token = SPISOR_INIT(forebay_accounts_token_argument);
    forebay_accounts_token_answer expired = SPISOR_INIT(forebay_accounts_token_answer);
    forebay_accounts_capacity_argument capacity = SPISOR_INIT(forebay_accounts_capacity_argument);
    forebay_accounts_capacity_answer has = SPISOR_INIT(forebay_accounts_capacity_answer);
    token.has_skew_milliseconds = 1;
    token.skew_milliseconds = -1;
    expect_status(accounts->token_expired(NULL, &token, &expired, &refusal), SPISOR_MALFORMED, &refusal,
                  "tokenExpired with a negative skew");
    token.skew_milliseconds = 1;
    token.expires = (double)INFINITY;
    expect_status(accounts->token_expired(NULL, &token, &expired, &refusal), SPISOR_MALFORMED, &refusal,
                  "tokenExpired with an infinite expiry");
    capacity.count = 2;
    expect_status(accounts->has_capacity(NULL, &capacity, &has, &refusal), SPISOR_MALFORMED, &refusal,
                  "hasCapacity with a count at no pools");
}

static void refuses_a_malformed_call(void) {
    char *directory = support_scratch();
    char *store = support_join(directory, "accounts.json");
    malformed_list_and_upsert(store);
    malformed_remove_and_acquire(store);
    malformed_report(store);
    malformed_token_and_capacity();
    if (file_exists(store)) {
        support_fail("malformed calls", "a malformed call created the store");
    }
    spisor_memory_release(store);
    spisor_memory_release(directory);
}

static spisor_status upsert_record(const char *store, const spisor_entry *entries, size_t count,
                                   spisor_refusal *refusal) {
    const forebay_accounts_definition *accounts = accounts_definition();
    spisor_value account = record_of(entries, count);
    forebay_accounts_upsert_argument upsert = SPISOR_INIT(forebay_accounts_upsert_argument);
    forebay_accounts_upsert_answer upserted = SPISOR_INIT(forebay_accounts_upsert_answer);
    upsert.store = store;
    upsert.account = &account;
    return accounts->upsert(NULL, &upsert, &upserted, refusal);
}

static void creates_the_missing_parent_directories_and_the_lock_file(void) {
    char *directory = support_scratch();
    char *inner = support_join3(directory, "one", "two");
    char *store = support_join(inner, "accounts.json");
    char *lock = support_join(inner, "accounts.json.lock");
    spisor_entry entries[1] = {id_entry("acc1")};
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    expect_status(upsert_record(store, entries, 1, &refusal), SPISOR_OK, &refusal, "upsert into missing directories");
    if (!file_exists(store)) {
        support_fail("missing directories", "the store was not created");
    }
    if (!file_exists(lock)) {
        support_fail("missing directories", "the lock file <store>.lock was not created");
    }
    spisor_memory_release(lock);
    spisor_memory_release(store);
    spisor_memory_release(inner);
    spisor_memory_release(directory);
}

static const char *const token_names[] = {"access", "refresh", "accessToken", "refreshToken", "idToken", "apiKey"};

static void token_members_are_refused(void) {
    char *directory = support_scratch();
    char *store = support_join(directory, "accounts.json");
    for (size_t index = 0; index < sizeof(token_names) / sizeof(token_names[0]); index++) {
        spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
        spisor_entry entries[2] = {id_entry("acc1"), {token_names[index], strlen(token_names[index]),
                                                      text_value("secret-value")}};
        spisor_status status = upsert_record(store, entries, 2, &refusal);
        if (status != SPISOR_REFUSED || refusal.detail == NULL || strstr(refusal.detail, token_names[index]) == NULL) {
            support_failf(token_names[index], "expected a refusal naming the member, got %d", (int)status);
        }
        accounts_definition()->release_refusal(NULL, &refusal);
    }
    if (file_exists(store)) {
        support_fail("token members", "a refused account reached the store");
    }
    spisor_memory_release(store);
    spisor_memory_release(directory);
}

static void report_on(const char *store, uint32_t outcome) {
    forebay_accounts_report_argument report = error_report(store);
    forebay_accounts_report_answer reported = SPISOR_INIT(forebay_accounts_report_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    report.id = "acc1";
    report.lane = "chat";
    report.outcome = outcome;
    report.has_reset_at = 1;
    report.reset_at = 9;
    expect_status(accounts_definition()->report(NULL, &report, &reported, &refusal), SPISOR_OK, &refusal, "report");
}

static void no_operation_writes_a_secret_looking_member(void) {
    char *directory = support_scratch();
    char *store = support_join(directory, "accounts.json");
    spisor_value expires = {SPISOR_VALUE_NUMBER, 0, 9, NULL, 0, NULL, NULL, 0};
    spisor_entry entries[3] = {id_entry("acc1"), {"label", 5, text_value("acc1")}, {"expires", 7, expires}};
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    forebay_accounts_acquire_argument acquire = SPISOR_INIT(forebay_accounts_acquire_argument);
    forebay_accounts_acquire_answer acquired = SPISOR_INIT(forebay_accounts_acquire_answer);
    expect_status(upsert_record(store, entries, 3, &refusal), SPISOR_OK, &refusal, "upsert");
    acquire.store = store;
    acquire.lane = "chat";
    acquire.strategy = FOREBAY_ACCOUNTS_HYBRID;
    acquire.now = 5;
    expect_status(accounts_definition()->acquire(NULL, &acquire, &acquired, &refusal), SPISOR_OK, &refusal, "acquire");
    accounts_definition()->release_acquire_answer(NULL, &acquired);
    for (uint32_t outcome = FOREBAY_ACCOUNTS_SUCCESS; outcome <= FOREBAY_ACCOUNTS_DISABLED; outcome++) {
        report_on(store, outcome);
    }
    char *text = support_read_text(store);
    if (text == NULL || strstr(text, "token") != NULL || strstr(text, "access\"") != NULL ||
        strstr(text, "refresh\"") != NULL) {
        support_fail("no secrets", text == NULL ? "the store is missing" : text);
    }
    free(text);
    spisor_memory_release(store);
    spisor_memory_release(directory);
}

static void whole_numbers_keep_their_text(void) {
    char *directory = support_scratch();
    char *store = support_join(directory, "accounts.json");
    spisor_value count = {SPISOR_VALUE_NUMBER, 0, 5, NULL, 0, NULL, NULL, 0};
    spisor_value fraction = {SPISOR_VALUE_NUMBER, 0, 0.5, NULL, 0, NULL, NULL, 0};
    spisor_entry meta_entries[2] = {{"count", 5, count}, {"remainingFraction", 17, fraction}};
    spisor_entry entries[2] = {id_entry("acc-meta"), {"meta", 4, record_of(meta_entries, 2)}};
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    expect_status(upsert_record(store, entries, 2, &refusal), SPISOR_OK, &refusal, "upsert meta");
    char *text = support_read_text(store);
    if (text == NULL || strstr(text, "\"count\": 5") == NULL || strstr(text, "\"count\": 5.0") != NULL ||
        strstr(text, "\"remainingFraction\": 0.5") == NULL) {
        support_fail("meta numbers", text == NULL ? "the store is missing" : text);
    }
    free(text);
    spisor_memory_release(store);
    spisor_memory_release(directory);
}

int main(void) {
    spisor_json_use_library_allocator();
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
