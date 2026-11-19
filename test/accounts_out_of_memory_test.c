#include "support.h"

#include "accounts_call.h"
#include "allocation.h"
#include "allocation_failure.h"
#include "spisor/json_allocation.h"

/**
 * @file
 * Fails every allocation of every accounts vector's first step, one at a time and from that one on, and requires each
 * failure to answer OUT_OF_MEMORY with the store's bytes as they were and every block released, or, where the failed
 * allocation was not one the call needed, the vector's own result. A failure is never read as an empty pool, a
 * refusal or a malformed call.
 */

static int same_text(const char *left, const char *right) {
    return (left == NULL && right == NULL) || (left != NULL && right != NULL && strcmp(left, right) == 0);
}

static void lay_out_store(const cJSON *vector, const char *store) {
    const cJSON *before = cJSON_GetObjectItemCaseSensitive(vector, "before");
    const cJSON *before_text = cJSON_GetObjectItemCaseSensitive(vector, "beforeText");
    remove(store);
    if (cJSON_IsObject(before)) {
        char *text = cJSON_PrintUnformatted(before);
        support_write(store, text);
        cJSON_free(text);
    } else if (cJSON_IsString(before_text)) {
        support_write(store, before_text->valuestring);
    }
}

typedef struct failed_call {
    spisor_status status;
    cJSON *result;
    int failed;
    int64_t leaked;
} failed_call;

static failed_call call_failing(const accounts_call *call, allocation_failure_mode mode, uint64_t attempt) {
    accounts_answers answers;
    failed_call outcome = {SPISOR_OK, NULL, 0, 0};
    int64_t baseline = spisor_memory_live_blocks();
    fail_allocation(mode, attempt);
    accounts_make_call(call, &answers);
    outcome.failed = spisor_memory_failed();
    spisor_memory_fail_at(0);
    outcome.status = answers.status;
    cJSON *result = accounts_result_of(call, &answers);
    static char printed[65536];
    if (!cJSON_PrintPreallocated(result, printed, (int)sizeof printed, 0)) {
        snprintf(printed, sizeof printed, "\"an answer too long to print\"");
    }
    cJSON_Delete(result);
    accounts_release_answers(&answers);
    outcome.leaked = spisor_memory_live_blocks() - baseline;
    outcome.result = cJSON_Parse(printed);
    return outcome;
}

/** Whether one failed attempt answered as required, reporting it when not. */
static int answered_as_required(const failed_call *outcome, const cJSON *expected, const char *before,
                                const char *after, const char *label, uint64_t attempt) {
    if (outcome->status == SPISOR_OUT_OF_MEMORY) {
        if (same_text(before, after)) {
            return 1;
        }
        support_failf(label, "failing allocation %llu answered out of memory after it wrote the store",
                      (unsigned long long)attempt);
        return 0;
    }
    if (cJSON_Compare(outcome->result, expected, 1)) {
        return 1;
    }
    char *got = cJSON_PrintUnformatted(outcome->result);
    support_failf(label, "failing allocation %llu answered status %d: %s", (unsigned long long)attempt,
                  (int)outcome->status, got);
    cJSON_free(got);
    return 0;
}

static int run_attempt(const cJSON *vector, const accounts_call *call, const char *store, const char *label,
                       allocation_failure_mode mode, uint64_t attempt) {
    lay_out_store(vector, store);
    char *before = support_read_text(store);
    failed_call outcome = call_failing(call, mode, attempt);
    char *after = support_read_text(store);
    int going_on = outcome.failed;
    if (outcome.leaked != 0) {
        support_failf(label, "failing allocation %llu left %lld blocks live", (unsigned long long)attempt,
                      (long long)outcome.leaked);
        going_on = 0;
    } else if (!outcome.failed && outcome.status != SPISOR_OK && outcome.status != SPISOR_REFUSED) {
        support_failf(label, "with no failure it answered %d", (int)outcome.status);
    } else if (outcome.failed &&
               !answered_as_required(&outcome, cJSON_GetObjectItemCaseSensitive(vector, "result"), before, after,
                                     label, attempt)) {
        going_on = 0;
    }
    cJSON_Delete(outcome.result);
    free(before);
    free(after);
    return going_on;
}

static void every_failure_of(const char *path, allocation_failure_mode mode) {
    cJSON *vector = spisor_vector_read(path);
    char *directory = support_scratch();
    char *store = support_join(directory, "accounts.json");
    accounts_call call;
    char label[1100];
    snprintf(label, sizeof label, "%s%s", path, allocation_failure_mode_name(mode));
    if (vector == NULL || !accounts_read_call(vector, store, &call)) {
        support_fail(label, "not a vector");
    } else {
        uint64_t attempt = 1;
        while (attempt < 100000 && run_attempt(vector, &call, store, label, mode, attempt)) {
            attempt++;
        }
        accounts_release_call(&call);
    }
    spisor_memory_release(store);
    spisor_memory_release(directory);
    cJSON_Delete(vector);
}

int main(void) {
    spisor_json_use_library_allocator();
    char *list = support_read_text(ACCOUNTS_VECTOR_LIST);
    int count = 0;
    if (list == NULL) {
        support_fail("vector list", ACCOUNTS_VECTOR_LIST);
        return 1;
    }
    for (char *line = strtok(list, "\r\n"); line != NULL; line = strtok(NULL, "\r\n")) {
        if (line[0] != '\0') {
            every_failure_of(line, FAIL_ONE_ALLOCATION);
            every_failure_of(line, FAIL_EVERY_LATER_ALLOCATION);
            count++;
        }
    }
    free(list);
    if (count == 0) {
        support_fail("vectors", "no accounts vector was found");
    }
    printf("accounts_out_of_memory_test: %d vectors, every allocation failed singly and for good\n", count);
    return support_summary("accounts_out_of_memory_test");
}
