#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "support.h"

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

int32_t forebay_library_accounts_list(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);
int32_t forebay_library_accounts_upsert(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);
int32_t forebay_library_accounts_report(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);

#define REPORTS_PER_KIND 200
#define WORKER_ARGUMENT "worker"

static support_answer call_with(apps_function function, cJSON *call) {
    char *input = cJSON_PrintUnformatted(call);
    support_answer answer = support_call_raw(function, input);
    free(input);
    cJSON_Delete(call);
    return answer;
}

static cJSON *report_call(const char *store, const char *id, const char *outcome, int now) {
    cJSON *call = cJSON_CreateObject();
    cJSON_AddStringToObject(call, "store", store);
    cJSON_AddStringToObject(call, "id", id);
    cJSON_AddStringToObject(call, "outcome", outcome);
    cJSON_AddNumberToObject(call, "now", now);
    return call;
}

static const cJSON *stored_account(const support_answer *listed, const char *id) {
    const cJSON *account = NULL;
    cJSON_ArrayForEach(account, support_at(listed->result, "accounts")) {
        const cJSON *candidate = cJSON_GetObjectItemCaseSensitive(account, "id");
        if (cJSON_IsString(candidate) && strcmp(candidate->valuestring, id) == 0) {
            return account;
        }
    }
    return NULL;
}

/** Whether the account's own last write is still in the store, read straight after its report returned. */
static int shows_member(const char *store, const char *id, const char *name, int expected, const char *lane) {
    cJSON *call = cJSON_CreateObject();
    cJSON_AddStringToObject(call, "store", store);
    support_answer listed = call_with(forebay_library_accounts_list, call);
    const cJSON *account = stored_account(&listed, id);
    const cJSON *holder = lane == NULL ? account : cJSON_GetObjectItemCaseSensitive(account, "rateLimitResetTimes");
    const cJSON *member = cJSON_GetObjectItemCaseSensitive(holder, lane == NULL ? name : lane);
    int shown = cJSON_IsNumber(member) && (int)member->valuedouble == expected;
    support_discard(&listed);
    return shown;
}

static int report_and_confirm(const char *store, const char *id, int step) {
    int failures = 0;
    support_answer success = call_with(forebay_library_accounts_report, report_call(store, id, "success", 1000 + step));
    support_discard(&success);
    if (!shows_member(store, id, "lastUsed", 1000 + step, NULL)) {
        fprintf(stderr, "%s: success %d was lost\n", id, step);
        failures++;
    }
    char lane[32];
    snprintf(lane, sizeof(lane), "lane-%d", step);
    cJSON *limited = report_call(store, id, "rate-limited", 1000 + step);
    cJSON_AddStringToObject(limited, "lane", lane);
    cJSON_AddNumberToObject(limited, "resetAt", 5000 + step);
    support_answer limit = call_with(forebay_library_accounts_report, limited);
    support_discard(&limit);
    if (!shows_member(store, id, NULL, 5000 + step, lane)) {
        fprintf(stderr, "%s: rate limit %d was lost\n", id, step);
        failures++;
    }
    return failures;
}

static int run_worker(const char *store, const char *id) {
    int failures = 0;
    for (int step = 0; step < REPORTS_PER_KIND; step++) {
        failures += report_and_confirm(store, id, step);
    }
    return failures == 0 ? 0 : 1;
}

static void add_account(const char *store, const char *id) {
    cJSON *account = cJSON_CreateObject();
    cJSON_AddStringToObject(account, "id", id);
    cJSON_AddStringToObject(account, "label", id);
    cJSON_AddBoolToObject(account, "enabled", 1);
    cJSON *call = cJSON_CreateObject();
    cJSON_AddStringToObject(call, "store", store);
    cJSON_AddItemToObject(call, "account", account);
    support_answer added = call_with(forebay_library_accounts_upsert, call);
    expect_json(&added, 0, "{\"outcome\":\"added\"}", id);
}

/** Starts this executable again as a worker; the handle comes back in started, 1 on success. */
static int start_worker(const char *self, const char *store, const char *id, void **started) {
#if defined(_WIN32)
    char quoted_self[1100];
    char quoted_store[1100];
    snprintf(quoted_self, sizeof(quoted_self), "\"%s\"", self);
    snprintf(quoted_store, sizeof(quoted_store), "\"%s\"", store);
    intptr_t handle = _spawnl(_P_NOWAIT, self, quoted_self, WORKER_ARGUMENT, quoted_store, id, (char *)NULL);
    *started = (void *)handle;
    return handle != -1;
#else
    pid_t child = fork();
    if (child == 0) {
        execl(self, self, WORKER_ARGUMENT, store, id, (char *)NULL);
        _exit(127);
    }
    *started = (void *)(intptr_t)child;
    return child > 0;
#endif
}

static int finish_worker(void *started) {
#if defined(_WIN32)
    int status = -1;
    if (_cwait(&status, (intptr_t)started, 0) == -1) {
        return -1;
    }
    return status;
#else
    int status = -1;
    if (waitpid((pid_t)(intptr_t)started, &status, 0) < 0) {
        return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

static void expect_account_effects(const char *store, const char *id) {
    cJSON *call = cJSON_CreateObject();
    cJSON_AddStringToObject(call, "store", store);
    support_answer listed = call_with(forebay_library_accounts_list, call);
    const cJSON *account = stored_account(&listed, id);
    const cJSON *limits = cJSON_GetObjectItemCaseSensitive(account, "rateLimitResetTimes");
    const cJSON *last_used = cJSON_GetObjectItemCaseSensitive(account, "lastUsed");
    if (account == NULL || cJSON_GetArraySize(limits) != REPORTS_PER_KIND) {
        support_failf(id, "expected %d lane resets, found %d", REPORTS_PER_KIND, cJSON_GetArraySize(limits));
    }
    if (!cJSON_IsNumber(last_used) || (int)last_used->valuedouble != 1000 + REPORTS_PER_KIND - 1) {
        support_fail(id, "the last success is not in the store");
    }
    for (int step = 0; step < REPORTS_PER_KIND; step++) {
        char lane[32];
        snprintf(lane, sizeof(lane), "lane-%d", step);
        const cJSON *reset = cJSON_GetObjectItemCaseSensitive(limits, lane);
        if (!cJSON_IsNumber(reset) || (int)reset->valuedouble != 5000 + step) {
            support_failf(id, "lane %s is missing or wrong", lane);
        }
    }
    support_discard(&listed);
}

static int run_parent(const char *self) {
    char *directory = support_scratch();
    char *store = support_join(directory, "accounts.json");
    add_account(store, "alpha");
    add_account(store, "beta");
    void *alpha = NULL;
    void *beta = NULL;
    if (!start_worker(self, store, "alpha", &alpha) || !start_worker(self, store, "beta", &beta)) {
        support_fail("workers", "could not start a worker process");
    } else {
        if (finish_worker(alpha) != 0) {
            support_fail("alpha", "its worker saw a lost update or failed");
        }
        if (finish_worker(beta) != 0) {
            support_fail("beta", "its worker saw a lost update or failed");
        }
        expect_account_effects(store, "alpha");
        expect_account_effects(store, "beta");
    }
    free(store);
    free(directory);
    printf("accounts_concurrent_test: %d effects per process\n", 2 * REPORTS_PER_KIND);
    return support_summary("accounts_concurrent_test");
}

int main(int argc, char **argv) {
    if (argc == 4 && strcmp(argv[1], WORKER_ARGUMENT) == 0) {
        return run_worker(argv[2], argv[3]);
    }
    return run_parent(argv[0]);
}
