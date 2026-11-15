#ifndef FOREBAY_ROUTING_H
#define FOREBAY_ROUTING_H

/**
 * @file
 * A spike of the `routing` service as a definition: `arrival` and `step` only, beside the JSON exports. Every
 * size-prefixed struct is read up to its `size` and its first shape ends at its last field; every element struct has
 * no size and never grows. Text is NUL-terminated UTF-8, borrowed for the call when the caller hands it in, and the
 * provider's until the matching release when it is answered.
 *
 * @implNote spisor_status and spisor_refusal are declared here only for the spike: their home is shared/library's
 * spisor/status.h, which the pinned library does not carry yet.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum spisor_status {
    SPISOR_OK = 0,
    SPISOR_NONE = 1,
    SPISOR_MALFORMED = 2,
    SPISOR_REFUSED = 3,
    SPISOR_OUT_OF_MEMORY = 4
} spisor_status;

typedef struct spisor_refusal {
    uint32_t size;
    const char *detail;
    const char *fix;
    void *storage;
} spisor_refusal;

typedef struct forebay_header {
    const char *name;
    const char *value;
} forebay_header;

typedef struct forebay_headers {
    const forebay_header *entries;
    size_t count;
} forebay_headers;

typedef struct forebay_texts {
    const char *const *items;
    size_t count;
} forebay_texts;

typedef struct forebay_arrival_argument {
    uint32_t size;
    const char *path;
    forebay_headers headers;
    uint32_t port;
} forebay_arrival_argument;

typedef enum forebay_arrival_kind {
    FOREBAY_ARRIVAL_ROUTE = 1,
    FOREBAY_ARRIVAL_HEALTH = 2,
    FOREBAY_ARRIVAL_REFUSE = 3
} forebay_arrival_kind;

typedef enum forebay_reply_kind {
    FOREBAY_REPLY_PROXY_ERROR = 1,
    FOREBAY_REPLY_API_ERROR = 2,
    FOREBAY_REPLY_PROVIDER_FAILURE = 3,
    FOREBAY_REPLY_RATE_LIMITED = 4
} forebay_reply_kind;

/** Route carries nothing; health a status, content type and body; refuse a proxyError reply of status and message. */
typedef struct forebay_arrival_answer {
    uint32_t size;
    uint32_t kind;
    uint32_t status;
    const char *content_type;
    const char *body;
    uint32_t reply_kind;
    const char *message;
    void *storage;
} forebay_arrival_answer;

typedef struct forebay_assignment {
    const char *provider;
    const char *model;
    const char *name;
    uint32_t derived;
} forebay_assignment;

/** One slot of the model map: a chain of assignments, best first. */
typedef struct forebay_model_slot {
    const char *slot;
    const forebay_assignment *chain;
    size_t count;
} forebay_model_slot;

typedef struct forebay_model_map {
    const forebay_model_slot *slots;
    size_t count;
} forebay_model_map;

typedef struct forebay_catalog_model {
    const char *id;
    /** NULL when the catalog names none. */
    const char *name;
} forebay_catalog_model;

typedef struct forebay_catalog_provider {
    const char *provider;
    const forebay_catalog_model *models;
    size_t model_count;
    forebay_texts ranking;
} forebay_catalog_provider;

typedef struct forebay_catalog {
    const forebay_catalog_provider *providers;
    size_t count;
} forebay_catalog;

/** The profile members `step` reads; a NULL pattern is one the profile does not declare. */
typedef struct forebay_profile {
    uint32_t size;
    const char *tier_source_provider;
    forebay_texts tier_order;
    forebay_texts tier_fallback;
    const char *tier_regex;
    const char *native_model_pattern;
    const char *unserved_model_pattern;
} forebay_profile;

/** One request's way along its chain, the provider's own: handed back unchanged, freed by `release_walk`. */
typedef struct forebay_walk forebay_walk;

typedef enum forebay_report_kind {
    FOREBAY_REPORT_RESPONSE = 1,
    FOREBAY_REPORT_STREAMED = 2,
    FOREBAY_REPORT_STREAM_FAILED = 3,
    FOREBAY_REPORT_THROWN = 4,
    FOREBAY_REPORT_FAILURE = 5
} forebay_report_kind;

typedef struct forebay_report {
    uint32_t size;
    uint32_t kind;
    const char *message;
    uint32_t after_bytes;
    double status;
    forebay_headers headers;
    double now;
} forebay_report;

/**
 * A first step when `walk` is NULL, which reads `profile` to `settings_file`; else a step reporting how the entry the
 * walk last called went, which reads `report`.
 */
typedef struct forebay_step_argument {
    uint32_t size;
    forebay_texts providers;
    const forebay_walk *walk;
    const forebay_profile *profile;
    forebay_model_map map;
    forebay_catalog models;
    const char *model;
    uint32_t pass_through;
    uint32_t refreshed;
    const char *login_command;
    const char *settings_file;
    const forebay_report *report;
} forebay_step_argument;

typedef enum forebay_step_kind {
    FOREBAY_STEP_PASS_THROUGH = 1,
    FOREBAY_STEP_REFRESH_MODELS = 2,
    FOREBAY_STEP_CALL_PROVIDER = 3,
    FOREBAY_STEP_REPLY = 4,
    FOREBAY_STEP_END_STREAM = 5,
    FOREBAY_STEP_DONE = 6
} forebay_step_kind;

typedef enum forebay_log_level {
    FOREBAY_LOG_INFO = 1,
    FOREBAY_LOG_WARN = 2
} forebay_log_level;

typedef struct forebay_log_line {
    uint32_t level;
    const char *message;
} forebay_log_line;

/**
 * callProvider fills `provider` and `model`; reply fills `reply_kind`, `status`, `message`, `headers` and `reset_at`
 * (0 for none) as its kind carries them; endStream fills `message` and `error_type`. `walk` is set when the step
 * expects a report, and is the caller's to hand back and to release, apart from this answer.
 */
typedef struct forebay_step_answer {
    uint32_t size;
    uint32_t kind;
    const char *provider;
    const char *model;
    uint32_t reply_kind;
    double status;
    const char *message;
    forebay_headers headers;
    double reset_at;
    const char *error_type;
    const forebay_log_line *log;
    size_t log_count;
    forebay_walk *walk;
    void *storage;
} forebay_step_answer;

/** The routing service. First shape ends at `release_refusal`. */
typedef struct forebay_routing_definition {
    uint32_t size;
    void *context;
    spisor_status (*arrival)(void *context, const forebay_arrival_argument *argument, forebay_arrival_answer *answer,
                             spisor_refusal *refusal);
    spisor_status (*step)(void *context, const forebay_step_argument *argument, forebay_step_answer *answer,
                          spisor_refusal *refusal);
    void (*release_arrival)(void *context, forebay_arrival_answer *answer);
    void (*release_step)(void *context, forebay_step_answer *answer);
    void (*release_walk)(void *context, forebay_walk *walk);
    void (*release_refusal)(void *context, spisor_refusal *refusal);
} forebay_routing_definition;

#if defined(_WIN32) && defined(FOREBAY_LIBRARY_BUILDING)
#define FOREBAY_ROUTING_API __declspec(dllexport)
#elif defined(_WIN32)
#define FOREBAY_ROUTING_API __declspec(dllimport)
#else
#define FOREBAY_ROUTING_API __attribute__((visibility("default")))
#endif

FOREBAY_ROUTING_API const forebay_routing_definition *forebay_library_routing_definition(void);

#ifdef __cplusplus
}
#endif

#endif
