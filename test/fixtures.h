#ifndef APPS_TEST_FIXTURES_H
#define APPS_TEST_FIXTURES_H

#include "support.h"

#define ALPHA                                                                                                     \
    "{\"id\":\"alpha\",\"label\":\"Alpha CLI\",\"home\":{\"envOverride\":\"HUB_ALPHA_DIR\",\"nativeEnv\":"       \
    "\"ALPHA_CONFIG_DIR\",\"candidates\":[\"~/.alpha\",\"~/.config/alpha\"]},\"detect\":{\"binary\":\"alpha\","   \
    "\"pkg\":\"alpha-cli\"},\"loader\":{\"id\":\"alpha-loader\",\"url\":\"acme-org/alpha-loader\"},"              \
    "\"commandsSubdir\":\"commands\",\"paths\":{\"repos\":\"repos\",\"plugin\":\"plugin\",\"cache\":\"cache\","   \
    "\"config\":\"config\"},\"proxyPort\":40001,\"integration\":\"env-baseurl\",\"wireFormat\":\"anthropic\","    \
    "\"usage\":{\"formats\":[\"alpha-jsonl\"]}}"

#define BETA                                                                                                      \
    "{\"id\":\"beta\",\"label\":\"Beta CLI\",\"home\":{\"envOverride\":\"HUB_BETA_DIR\",\"nativeEnv\":"          \
    "\"BETA_CONFIG_DIR\",\"xdgSubdir\":\"beta\",\"candidates\":[\"~/.config/beta\",\"~/.beta\"]},\"detect\":"     \
    "{\"binary\":\"beta\",\"pkg\":\"beta-cli\"},\"commandsSubdir\":\"command\",\"proxyPort\":40002,"              \
    "\"integration\":\"native\",\"wireFormat\":\"anthropic\"}"

/** One case's temporary home with the registry at <home>/apps.json, named by HUB_APPS_FILE as basekit's tests do. */
typedef struct registry_case {
    char *home;
    char *file;
} registry_case;

static inline registry_case registry_case_open(void) {
    registry_case opened;
    opened.home = support_scratch();
    opened.file = support_join(opened.home, "apps.json");
    return opened;
}

static inline void registry_case_close(registry_case *closed) {
    free(closed->home);
    free(closed->file);
}

static inline cJSON *registry_context(const registry_case *open) {
    return support_context(open->home, "HUB_APPS_FILE", open->file, NULL);
}

static inline support_answer list_text(const char *registry) {
    registry_case open = registry_case_open();
    support_write(open.file, registry);
    support_answer answer = support_call(forebay_library_apps_list, "{}", registry_context(&open));
    registry_case_close(&open);
    return answer;
}

static inline support_answer list_bytes(const char *bytes, size_t length) {
    registry_case open = registry_case_open();
    support_write_bytes(open.file, bytes, length);
    support_answer answer = support_call(forebay_library_apps_list, "{}", registry_context(&open));
    registry_case_close(&open);
    return answer;
}

#endif
