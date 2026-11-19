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

static inline int support_summary(const char *suite) {
    if (support_failures == 0) {
        printf("%s: all checks passed\n", suite);
        return 0;
    }
    fprintf(stderr, "%s: %d checks failed\n", suite, support_failures);
    return 1;
}

#endif
