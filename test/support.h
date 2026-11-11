#ifndef LIBRARY_TEST_SUPPORT_H
#define LIBRARY_TEST_SUPPORT_H

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "spisor/path.h"

#if defined(_WIN32)
#include <direct.h>
#include <process.h>
#define SUPPORT_WINDOWS 1
#define support_make_directory(path) _mkdir(path)
#define support_process_id() _getpid()
#else
#include <sys/stat.h>
#include <unistd.h>
#define SUPPORT_WINDOWS 0
#define support_make_directory(path) mkdir(path, 0777)
#define support_process_id() getpid()
#endif

typedef int32_t (*library_function)(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);

void forebay_library_release(uint8_t *buffer);

static int support_failures = 0;
static int support_case_count = 0;
static char support_run_root[1024];

static inline void support_failf(const char *label, const char *format, ...) {
    va_list values;
    va_start(values, format);
    fprintf(stderr, "FAIL %s: ", label);
    vfprintf(stderr, format, values);
    fputc('\n', stderr);
    va_end(values);
    support_failures++;
}

static inline void support_fail(const char *label, const char *detail) {
    support_failf(label, "%s", detail);
}

static inline char *support_join(const char *first, const char *second) {
    const char *segments[] = {first, second};
    return spisor_path_join(SUPPORT_WINDOWS, segments, 2);
}

static inline char *support_join3(const char *first, const char *second, const char *third) {
    const char *segments[] = {first, second, third};
    return spisor_path_join(SUPPORT_WINDOWS, segments, 3);
}

/** A fresh directory per case, under a root unique to this run, so a re-run never sees old state. */
static inline char *support_scratch(void) {
    if (support_run_root[0] == '\0') {
        snprintf(support_run_root, sizeof(support_run_root), "%s/run-%ld-%d", LIBRARY_SCRATCH_PATH,
                 (long)time(NULL), (int)support_process_id());
        support_make_directory(support_run_root);
    }
    char name[32];
    snprintf(name, sizeof(name), "case-%d", ++support_case_count);
    char *directory = support_join(support_run_root, name);
    support_make_directory(directory);
    return directory;
}

static inline void support_write(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        support_fail("write", path);
        return;
    }
    fwrite(text, 1, strlen(text), file);
    fclose(file);
}

/** The whole file as a NUL terminated string, freed with free, or NULL when it cannot be read. */
static inline char *support_read_text(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *text = malloc((size_t)size + 1);
    size_t read = text == NULL ? 0 : fread(text, 1, (size_t)size, file);
    fclose(file);
    if (text != NULL) {
        text[read] = '\0';
    }
    return text;
}

typedef struct support_answer {
    int32_t status;
    cJSON *result;
    char text[8192];
} support_answer;

static inline support_answer support_call_raw(library_function function, const char *input) {
    support_answer answer;
    uint8_t *out = NULL;
    size_t out_len = 0;
    answer.status = function((const uint8_t *)input, strlen(input), &out, &out_len);
    size_t kept = out_len < sizeof(answer.text) - 1 ? out_len : sizeof(answer.text) - 1;
    if (out != NULL) {
        memcpy(answer.text, out, kept);
    }
    answer.text[kept] = '\0';
    answer.result = out == NULL ? NULL : cJSON_ParseWithLength((const char *)out, out_len);
    forebay_library_release(out);
    return answer;
}

static inline void support_discard(support_answer *answer) {
    cJSON_Delete(answer->result);
    answer->result = NULL;
}

static inline void expect_json(support_answer *answer, int32_t status, const char *expected, const char *label) {
    cJSON *wanted = cJSON_Parse(expected);
    if (wanted == NULL) {
        support_fail(label, "the expectation is not JSON");
    } else if (answer->status != status || answer->result == NULL || !cJSON_Compare(answer->result, wanted, 1)) {
        support_failf(label, "status %d, expected %d\n  expected %s\n  actual   %s", (int)answer->status,
                 (int)status, expected, answer->text);
    }
    cJSON_Delete(wanted);
    support_discard(answer);
}

/** A failure object carrying a non-empty message and fix, with status 1. */
static inline void expect_failure(support_answer *answer, const char *label) {
    const cJSON *message = cJSON_GetObjectItemCaseSensitive(answer->result, "message");
    const cJSON *fix = cJSON_GetObjectItemCaseSensitive(answer->result, "fix");
    if (answer->status != 1 || !cJSON_IsString(message) || !cJSON_IsString(fix) || message->valuestring[0] == '\0' ||
        fix->valuestring[0] == '\0') {
        support_failf(label, "expected a failure object, got status %d: %s", (int)answer->status,
                 answer->text);
    }
    support_discard(answer);
}

/** The value at a dotted path such as "accounts.0.id", for asserting one field. */
static inline const cJSON *support_at(const cJSON *value, const char *path) {
    char key[256];
    while (value != NULL && *path != '\0') {
        size_t length = strcspn(path, ".");
        memcpy(key, path, length);
        key[length] = '\0';
        if (cJSON_IsArray(value)) {
            value = cJSON_GetArrayItem(value, atoi(key));
        } else {
            value = cJSON_GetObjectItemCaseSensitive(value, key);
        }
        path += length + (path[length] == '.' ? 1 : 0);
    }
    return value;
}

static inline int support_summary(const char *suite) {
    if (support_failures == 0) {
        printf("%s: all checks passed\n", suite);
        return 0;
    }
    fprintf(stderr, "%s: %d checks failed\n", suite, support_failures);
    return 1;
}

#endif
