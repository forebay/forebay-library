#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "support.h"

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

#include "forebay/accounts.h"
#include "spisor/allocation.h"

#define REPORTS_PER_KIND 200
#define WORKER_ARGUMENT "worker"

static const forebay_accounts_definition *accounts(void) {
    return forebay_library_accounts_definition();
}

static spisor_status report(const char *store, const char *id, uint32_t outcome, int now, const char *lane,
                            int reset_at) {
    forebay_accounts_report_argument argument = SPISOR_INIT(forebay_accounts_report_argument);
    forebay_accounts_report_answer answer = SPISOR_INIT(forebay_accounts_report_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    argument.store = store;
    argument.id = id;
    argument.outcome = outcome;
    argument.now = now;
    argument.lane = lane;
    argument.has_reset_at = lane != NULL;
    argument.reset_at = reset_at;
    spisor_status status = accounts()->report(NULL, &argument, &answer, &refusal);
    accounts()->release_refusal(NULL, &refusal);
    return status;
}

static spisor_status list(const char *store, forebay_accounts_list_answer *answer) {
    forebay_accounts_list_argument argument = SPISOR_INIT(forebay_accounts_list_argument);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    argument.store = store;
    spisor_status status = accounts()->list(NULL, &argument, answer, &refusal);
    accounts()->release_refusal(NULL, &refusal);
    return status;
}

static const spisor_value *member_of(const spisor_value *record, const char *key) {
    return record == NULL ? NULL : spisor_value_member(record, key, strlen(key));
}

static const spisor_value *stored_account(const forebay_accounts_list_answer *listed, const char *id) {
    for (size_t index = 0; index < listed->count; index++) {
        const spisor_value *candidate = member_of(&listed->accounts[index], "id");
        if (candidate != NULL && candidate->kind == SPISOR_VALUE_TEXT && strcmp(candidate->text, id) == 0) {
            return &listed->accounts[index];
        }
    }
    return NULL;
}

static int is_number(const spisor_value *value, int expected) {
    return value != NULL && value->kind == SPISOR_VALUE_NUMBER && (int)value->number == expected;
}

/** Whether the account's own last write is still in the store, read straight after its report returned. */
static int shows_member(const char *store, const char *id, const char *name, int expected, const char *lane) {
    forebay_accounts_list_answer listed = SPISOR_INIT(forebay_accounts_list_answer);
    list(store, &listed);
    const spisor_value *account = stored_account(&listed, id);
    const spisor_value *holder = lane == NULL ? account : member_of(account, "rateLimitResetTimes");
    int shown = is_number(member_of(holder, lane == NULL ? name : lane), expected);
    accounts()->release_list_answer(NULL, &listed);
    return shown;
}

static int report_and_confirm(const char *store, const char *id, int step) {
    int failures = 0;
    report(store, id, FOREBAY_ACCOUNTS_SUCCESS, 1000 + step, NULL, 0);
    if (!shows_member(store, id, "lastUsed", 1000 + step, NULL)) {
        fprintf(stderr, "%s: success %d was lost\n", id, step);
        failures++;
    }
    char lane[32];
    snprintf(lane, sizeof(lane), "lane-%d", step);
    report(store, id, FOREBAY_ACCOUNTS_RATE_LIMITED, 1000 + step, lane, 5000 + step);
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
    spisor_value id_value = {SPISOR_VALUE_TEXT, 0, 0, id, strlen(id), NULL, NULL, 0};
    spisor_value enabled = {SPISOR_VALUE_BOOLEAN, 1, 0, NULL, 0, NULL, NULL, 0};
    spisor_entry entries[3] = {{"id", 2, id_value}, {"label", 5, id_value}, {"enabled", 7, enabled}};
    spisor_value account = {SPISOR_VALUE_RECORD, 0, 0, NULL, 0, NULL, entries, 3};
    forebay_accounts_upsert_argument argument = SPISOR_INIT(forebay_accounts_upsert_argument);
    forebay_accounts_upsert_answer answer = SPISOR_INIT(forebay_accounts_upsert_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    argument.store = store;
    argument.account = &account;
    if (accounts()->upsert(NULL, &argument, &answer, &refusal) != SPISOR_OK ||
        answer.outcome != FOREBAY_ACCOUNTS_ADDED) {
        support_fail(id, "the account was not added");
    }
    accounts()->release_refusal(NULL, &refusal);
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
    forebay_accounts_list_answer listed = SPISOR_INIT(forebay_accounts_list_answer);
    list(store, &listed);
    const spisor_value *account = stored_account(&listed, id);
    const spisor_value *limits = member_of(account, "rateLimitResetTimes");
    size_t limit_count = limits == NULL ? 0 : limits->count;
    if (account == NULL || limit_count != REPORTS_PER_KIND) {
        support_failf(id, "expected %d lane resets, found %d", REPORTS_PER_KIND, (int)limit_count);
    }
    if (!is_number(member_of(account, "lastUsed"), 1000 + REPORTS_PER_KIND - 1)) {
        support_fail(id, "the last success is not in the store");
    }
    for (int step = 0; step < REPORTS_PER_KIND; step++) {
        char lane[32];
        snprintf(lane, sizeof(lane), "lane-%d", step);
        if (!is_number(member_of(limits, lane), 5000 + step)) {
            support_failf(id, "lane %s is missing or wrong", lane);
        }
    }
    accounts()->release_list_answer(NULL, &listed);
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
    spisor_memory_release(store);
    spisor_memory_release(directory);
    printf("accounts_concurrent_test: %d effects per process\n", 2 * REPORTS_PER_KIND);
    return support_summary("accounts_concurrent_test");
}

int main(int argc, char **argv) {
    if (argc == 4 && strcmp(argv[1], WORKER_ARGUMENT) == 0) {
        return run_worker(argv[2], argv[3]);
    }
    return run_parent(argv[0]);
}
