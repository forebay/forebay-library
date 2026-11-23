#ifndef FOREBAY_PUBLIC_ROUTING_H
#define FOREBAY_PUBLIC_ROUTING_H

/**
 * @file
 * forebay-library's `routing` service: the proxy's decisions, from whether a request is admitted, through which
 * provider and model serve it and what to do when one fails, to the environment an application starts with. Every
 * decision reads only its argument, apart from `environment`'s state file and a catalog, the one state routing keeps,
 * which the host opens and hands back. A size-prefixed struct is read only up to its `size`; an array element has no
 * size, and changes only with the declaration's version. Text the caller hands in is NUL-terminated UTF-8 borrowed for
 * the call; what an answer or a refusal points at is the provider's until the release for its kind. An array is
 * `count` elements at its pointer, which may be NULL only when `count` is 0. In a list of `forebay_pair`, headers or
 * environment variables, a later pair with a name already given replaces the earlier one.
 */

#include <stddef.h>
#include <stdint.h>

#include "forebay/definition.h"
#include "forebay/pair.h"
#include "forebay/reply.h"
#include "spisor/status.h"
#include "spisor/value.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * One provider's model a model-map slot is served by: `name` its display name, NULL in an argument for the model id,
 * and `derived` 1 when no one chose it.
 */
typedef struct forebay_assignment {
    const char *provider;
    const char *model;
    const char *name;
    uint32_t derived;
} forebay_assignment;

/** One slot of the model map, such as `opus`: its chain of assignments, best first. */
typedef struct forebay_slot {
    const char *slot;
    const forebay_assignment *chain;
    size_t count;
} forebay_slot;

/**
 * One model a provider serves; `name` NULL when the catalog names none, and `context` and `output` its token limits
 * when `has_context` and `has_output` are 1.
 */
typedef struct forebay_catalog_model {
    const char *id;
    const char *name;
    uint32_t has_context;
    uint64_t context;
    uint32_t has_output;
    uint64_t output;
} forebay_catalog_model;

/**
 * One provider's models in catalog order, and its ranking of model ids, best first, when `has_ranking` is 1; without
 * one its catalog order ranks them. `default_model` is the id the provider names its default, NULL for none.
 */
typedef struct forebay_catalog_provider {
    const char *provider;
    const forebay_catalog_model *models;
    size_t model_count;
    uint32_t has_ranking;
    const char *const *ranking;
    size_t ranking_count;
    const char *default_model;
} forebay_catalog_provider;

/**
 * The profile members routing reads. The tier members say how a model id names its tier: `tier_regex`, with one
 * group, over `tier_source_provider`'s ranked ids, sorted by `tier_order`, else `tier_fallback`. A NULL pattern is
 * one the profile does not declare. `default_context` and `default_output` are the token limits of a model whose
 * catalog entry gives none. First shape ends at `unserved_model_pattern`.
 */
typedef struct forebay_routing_profile {
    uint32_t size;
    const char *tier_source_provider;
    const char *const *tier_order;
    size_t tier_order_count;
    const char *const *tier_fallback;
    size_t tier_fallback_count;
    const char *tier_regex;
    const char *native_model_pattern;
    const char *unserved_model_pattern;
    uint64_t default_context;
    uint64_t default_output;
} forebay_routing_profile;

/**
 * The tiers of a profile over a catalog; the tier members of `profile` are required. First shape ends at
 * `provider_count`.
 */
typedef struct forebay_tiers_argument {
    uint32_t size;
    const forebay_routing_profile *profile;
    const forebay_catalog_provider *providers;
    size_t provider_count;
} forebay_tiers_argument;

/** The tiers, best first. First shape ends at `storage`. */
typedef struct forebay_tiers_answer {
    uint32_t size;
    const char *const *tiers;
    size_t count;
    void *storage;
} forebay_tiers_answer;

/**
 * The model map a profile derives over a catalog, kept where `settings`, the user's settings record, chose a slot
 * the catalog still serves; the tier members of `profile` are required. First shape ends at `provider_count`.
 */
typedef struct forebay_model_map_argument {
    uint32_t size;
    const forebay_routing_profile *profile;
    const spisor_value *settings;
    const forebay_catalog_provider *providers;
    size_t provider_count;
} forebay_model_map_argument;

/**
 * Every slot, and the slots whose choice was replaced because the catalog no longer serves it. First shape ends at
 * `storage`.
 */
typedef struct forebay_model_map_answer {
    uint32_t size;
    const forebay_slot *slots;
    size_t slot_count;
    const char *const *healed;
    size_t healed_count;
    void *storage;
} forebay_model_map_answer;

/** The chain that serves `requested_model`. First shape ends at `requested_model`. */
typedef struct forebay_resolve_argument {
    uint32_t size;
    const forebay_routing_profile *profile;
    const forebay_slot *slots;
    size_t slot_count;
    const forebay_catalog_provider *providers;
    size_t provider_count;
    const char *requested_model;
} forebay_resolve_argument;

/** What a resolve noticed. Append-only: a published value keeps its number. */
typedef enum forebay_resolve_notice {
    FOREBAY_MODEL_SWITCHED = 1,
    FOREBAY_HEALED = 2
} forebay_resolve_notice;

/**
 * The chain, best first, and the notices it raised, each FOREBAY_MODEL_SWITCHED or FOREBAY_HEALED. First shape ends
 * at `storage`.
 */
typedef struct forebay_resolve_answer {
    uint32_t size;
    const forebay_assignment *chain;
    size_t chain_count;
    const uint32_t *notices;
    size_t notice_count;
    void *storage;
} forebay_resolve_answer;

/**
 * The models an application lists over a catalog, with the profile's default limits for a model that gives none;
 * the tier members of `profile` are not read. First shape ends at `catalog_count`.
 */
typedef struct forebay_model_entries_argument {
    uint32_t size;
    const forebay_routing_profile *profile;
    const forebay_catalog_provider *catalog;
    size_t catalog_count;
} forebay_model_entries_argument;

/**
 * Every model id the catalog serves, once, in catalog order, each with `name` the catalog's, or the id when the catalog
 * names none, and each limit the model's, else the profile's default, present when its `has_` flag is 1. First shape
 * ends at `storage`.
 */
typedef struct forebay_model_entries_answer {
    uint32_t size;
    const forebay_catalog_model *entries;
    size_t entry_count;
    void *storage;
} forebay_model_entries_answer;

/**
 * An upstream response's status and headers at `now`, and the retry an earlier reply named when
 * `has_retry_after` is 1. First shape ends at `retry_after_milliseconds`.
 */
typedef struct forebay_outcome_argument {
    uint32_t size;
    uint32_t status;
    const forebay_pair *headers;
    size_t header_count;
    double now;
    uint32_t has_retry_after;
    double retry_after_milliseconds;
} forebay_outcome_argument;

/**
 * Whether the response limited the rate, and until when when `has_reset_at` is 1. It owns nothing. First shape ends
 * at `reset_at`.
 */
typedef struct forebay_outcome_answer {
    uint32_t size;
    uint32_t rate_limited;
    uint32_t has_reset_at;
    double reset_at;
} forebay_outcome_answer;

/**
 * One request's way along its chain, the provider's own: never changed once answered, so it may be handed back more
 * than once and from any thread. Freed by `release_walk` only after every step it was handed to has returned.
 */
typedef struct forebay_walk forebay_walk;

/** How the entry a walk last called went. Append-only: a published value keeps its number. */
typedef enum forebay_report_kind {
    FOREBAY_REPORT_RESPONSE = 1,
    FOREBAY_REPORT_STREAMED = 2,
    FOREBAY_REPORT_STREAM_FAILED = 3,
    FOREBAY_REPORT_THROWN = 4,
    FOREBAY_REPORT_FAILURE = 5
} forebay_report_kind;

/**
 * How an entry went and what it carries: `after_bytes` 1 when a stream failed after bytes went out, `message` (NULL for
 * none) for a throw or a failed stream, and the upstream `status`, 0 for none or an HTTP status from 100 to 599, and
 * `headers` at `now` for a failure. First shape ends at `now`.
 */
typedef struct forebay_report {
    uint32_t size;
    /** A forebay_report_kind. */
    uint32_t kind;
    uint32_t after_bytes;
    const char *message;
    uint32_t status;
    const forebay_pair *headers;
    size_t header_count;
    uint32_t has_retry_after;
    double retry_after_milliseconds;
    double now;
} forebay_report;

/**
 * A first step when `walk` is NULL, which reads everything up to `settings_file`: `model` the model asked for, NULL
 * when the request names none, and `pass_through` 1 to send it upstream undecoded. A later step hands back the walk an
 * answer gave and the `report` of how its entry went. First shape ends at `report`.
 */
typedef struct forebay_step_argument {
    uint32_t size;
    const char *const *providers;
    size_t provider_count;
    const forebay_walk *walk;
    const forebay_routing_profile *profile;
    const forebay_slot *slots;
    size_t slot_count;
    const forebay_catalog_provider *catalog;
    size_t catalog_count;
    const char *model;
    uint32_t pass_through;
    uint32_t refreshed;
    const char *login_command;
    const char *settings_file;
    const forebay_report *report;
} forebay_step_argument;

/** What the caller does next. Append-only: a published value keeps its number. */
typedef enum forebay_step_kind {
    FOREBAY_STEP_PASS_THROUGH = 1,
    FOREBAY_STEP_REFRESH_MODELS = 2,
    FOREBAY_STEP_CALL_PROVIDER = 3,
    FOREBAY_STEP_REPLY = 4,
    FOREBAY_STEP_END_STREAM = 5,
    FOREBAY_STEP_DONE = 6
} forebay_step_kind;

/** A log line's level. Append-only: a published value keeps its number. */
typedef enum forebay_log_level {
    FOREBAY_LOG_INFO = 1,
    FOREBAY_LOG_WARN = 2
} forebay_log_level;

/** One line for the proxy's log. */
typedef struct forebay_log_line {
    /** A forebay_log_level. */
    uint32_t level;
    const char *message;
} forebay_log_line;

/**
 * What a step answers. Call provider fills `provider` and `model`. Reply fills `reply_kind`: a proxy or API error its
 * `status` and `message`, a provider failure its `status` and the `headers` to forward, a rate limit those headers and,
 * when `has_reset_at` is 1, `reset_at`. End stream fills `error_type` and `message`. `walk` is set when the step
 * expects a report, and is the caller's to hand back and to release apart from this answer. First shape ends at
 * `storage`.
 */
typedef struct forebay_step_answer {
    uint32_t size;
    /** A forebay_step_kind. */
    uint32_t kind;
    const char *provider;
    const char *model;
    /** A forebay_reply_kind. */
    uint32_t reply_kind;
    uint32_t status;
    const char *message;
    const forebay_pair *headers;
    size_t header_count;
    uint32_t has_reset_at;
    double reset_at;
    const char *error_type;
    const forebay_log_line *log;
    size_t log_count;
    forebay_walk *walk;
    void *storage;
} forebay_step_answer;

/**
 * The proxy's fixed limits: a request larger than `max_request_bytes` is answered with the too large reply, of
 * `too_large_reply_kind`, `too_large_status` and `too_large_message`, static text. `models_budget_milliseconds`,
 * `rederive_interval_milliseconds` and `refresh_every_milliseconds` are the numbers a catalog decides with; a host
 * reads the budget and every timer's due time from the catalog's answers. It owns nothing. First shape ends at
 * `refresh_every_milliseconds`.
 */
typedef struct forebay_limits_answer {
    uint32_t size;
    uint64_t max_request_bytes;
    uint32_t too_large_status;
    const char *too_large_message;
    uint64_t models_budget_milliseconds;
    uint64_t rederive_interval_milliseconds;
    uint64_t refresh_every_milliseconds;
    /** A forebay_reply_kind. */
    uint32_t too_large_reply_kind;
} forebay_limits_answer;

/**
 * The catalog a host keeps of its providers' models, the provider's from catalog_open until release_catalog: its
 * rounds of asks, each implementation's last answer, the model map derived over them and the entries listed from
 * them. It is used by one thread at a time. An event that answers anything but OK leaves it as it was, so the same
 * event may be reported again.
 */
typedef struct forebay_catalog forebay_catalog;

/** What a host reports to its catalog. Append-only: a published value keeps its number. */
typedef enum forebay_catalog_event_kind {
    FOREBAY_CATALOG_READ = 1,
    FOREBAY_CATALOG_NO_MODEL = 2,
    FOREBAY_CATALOG_ANSWERED = 3,
    FOREBAY_CATALOG_TIMER = 4
} forebay_catalog_event_kind;

/** What a catalog timer does when it fires. Append-only: a published value keeps its number. */
typedef enum forebay_catalog_timer_kind {
    FOREBAY_CATALOG_ROUND_CLOSE = 1,
    FOREBAY_CATALOG_PERIOD = 2
} forebay_catalog_timer_kind;

/** What a host does with a timer. Append-only: a published value keeps its number. */
typedef enum forebay_catalog_timer_change {
    FOREBAY_CATALOG_SET = 1,
    FOREBAY_CATALOG_CLEAR = 2
} forebay_catalog_timer_change;

/**
 * One timer to set, replacing any of the same `kind` and `round`, due at `due`, epoch milliseconds, or to clear. A
 * round close timer names its round, from 1; the period timer names round 0.
 */
typedef struct forebay_catalog_timer {
    /** A forebay_catalog_timer_change. */
    uint32_t change;
    /** A forebay_catalog_timer_kind. */
    uint32_t kind;
    uint64_t round;
    double due;
} forebay_catalog_timer;

/**
 * A new catalog at `now`, epoch milliseconds, deriving its model map as `model_map` does with `profile`, whose tier
 * members are required, and `settings`, the user's settings record, both copied. First shape ends at `settings`.
 */
typedef struct forebay_catalog_open_argument {
    uint32_t size;
    double now;
    const forebay_routing_profile *profile;
    const spisor_value *settings;
} forebay_catalog_open_argument;

/**
 * The catalog, the caller's to hand to every event and read and to release apart from this answer, and the timer to
 * set, the period of `limits`' `refresh_every_milliseconds`. First shape ends at `storage`.
 */
typedef struct forebay_catalog_open_answer {
    uint32_t size;
    forebay_catalog *catalog;
    const forebay_catalog_timer *timers;
    size_t timer_count;
    void *storage;
} forebay_catalog_open_answer;

/**
 * One event of `catalog` at `now`, epoch milliseconds. READ: a request wants the catalog, sent by every request before
 * it reads. NO_MODEL: a step answered FOREBAY_STEP_REFRESH_MODELS. Both carry `implementations`, the host's ordered
 * list of the implementations it offers, each named once by a text the host chooses. ANSWERED: the ask of
 * `implementation` in `round` settled, with `models`, its answer, or with `failed` 1 and its `message`. TIMER: the
 * `timer` of `round` fired, a period timer carrying `implementations` too. First shape ends at `timer`.
 */
typedef struct forebay_catalog_event_argument {
    uint32_t size;
    forebay_catalog *catalog;
    /** A forebay_catalog_event_kind. */
    uint32_t kind;
    double now;
    const char *const *implementations;
    size_t implementation_count;
    uint64_t round;
    const char *implementation;
    const forebay_catalog_provider *models;
    uint32_t failed;
    const char *message;
    /** A forebay_catalog_timer_kind. */
    uint32_t timer;
} forebay_catalog_event_argument;

/**
 * What to do about the event. `asks`: ask each of these implementations, reporting each answer with `ask_round`, which
 * is set whenever a round starts, within `budget_milliseconds`. `timers`: the timers to set or clear, in order.
 * `wait_round`: the round a READ or NO_MODEL waits on, 0 when it reads the catalog now. `released`: the rounds this
 * event closed, in order, the closing round last and before it every round it superseded while they were open,
 * whether or not a request waits on any of them; a request waiting on one reads the catalog now. `log` the lines to
 * write. First shape ends at `storage`.
 */
typedef struct forebay_catalog_event_answer {
    uint32_t size;
    uint64_t wait_round;
    uint64_t ask_round;
    const char *const *asks;
    size_t ask_count;
    uint64_t budget_milliseconds;
    const forebay_catalog_timer *timers;
    size_t timer_count;
    const uint64_t *released;
    size_t released_count;
    const forebay_log_line *log;
    size_t log_count;
    void *storage;
} forebay_catalog_event_answer;

/** The catalog to read. First shape ends at `catalog`. */
typedef struct forebay_catalog_read_argument {
    uint32_t size;
    const forebay_catalog *catalog;
} forebay_catalog_read_argument;

/**
 * The catalog as it stands: every implementation the latest round asked that has an answer kept, in that round's
 * order, beside its answer at the same index of `providers`, the catalog a step reads; the model map last derived;
 * and the entries `model_entries` lists over those providers. First shape ends at `storage`.
 */
typedef struct forebay_catalog_read_answer {
    uint32_t size;
    const char *const *implementations;
    size_t implementation_count;
    const forebay_catalog_provider *providers;
    size_t provider_count;
    const forebay_slot *slots;
    size_t slot_count;
    const forebay_catalog_model *entries;
    size_t entry_count;
    void *storage;
} forebay_catalog_read_answer;

/**
 * A request to send upstream undecoded, with the placeholder authorization a client sent. First shape ends at
 * `placeholder_authorization`.
 */
typedef struct forebay_pass_through_argument {
    uint32_t size;
    const char *upstream;
    const char *url;
    const forebay_pair *headers;
    size_t header_count;
    uint64_t body_length;
    const char *placeholder_authorization;
} forebay_pass_through_argument;

/** The upstream URL and the headers to send it. First shape ends at `storage`. */
typedef struct forebay_pass_through_answer {
    uint32_t size;
    const char *target;
    const forebay_pair *headers;
    size_t header_count;
    void *storage;
} forebay_pass_through_answer;

/** An upstream reply's headers, and whether its body was rewritten. First shape ends at `rewritten`. */
typedef struct forebay_reply_headers_argument {
    uint32_t size;
    const forebay_pair *headers;
    size_t header_count;
    uint32_t rewritten;
} forebay_reply_headers_argument;

/** The headers fit to send the client. First shape ends at `storage`. */
typedef struct forebay_reply_headers_answer {
    uint32_t size;
    const forebay_pair *headers;
    size_t header_count;
    void *storage;
} forebay_reply_headers_answer;

/** A request as it arrives at the proxy listening on `port`, from 1 to 65535. First shape ends at `port`. */
typedef struct forebay_arrival_argument {
    uint32_t size;
    const char *path;
    const forebay_pair *headers;
    size_t header_count;
    uint32_t port;
} forebay_arrival_argument;

/** What an arrival is. Append-only: a published value keeps its number. */
typedef enum forebay_arrival_kind {
    FOREBAY_ARRIVAL_ROUTE = 1,
    FOREBAY_ARRIVAL_HEALTH = 2,
    FOREBAY_ARRIVAL_REFUSE = 3
} forebay_arrival_kind;

/**
 * Route carries nothing; health a `status`, and a `content_type` and `body` that are static text; refuse a reply of
 * `reply_kind`, `status` and `message`. First shape ends at `storage`.
 */
typedef struct forebay_arrival_answer {
    uint32_t size;
    /** A forebay_arrival_kind. */
    uint32_t kind;
    uint32_t status;
    const char *content_type;
    const char *body;
    /** A forebay_reply_kind. */
    uint32_t reply_kind;
    const char *message;
    void *storage;
} forebay_arrival_answer;

/**
 * Why the proxy could not listen on the port: `in_use` 1 when another listener holds it. First shape ends at
 * `detail`.
 */
typedef struct forebay_port_failure {
    uint32_t size;
    uint32_t in_use;
    const char *detail;
} forebay_port_failure;

/**
 * The `proxy.port` setting as `settings_file` holds it, any value, and the failure listening on it, NULL for none.
 * First shape ends at `failure`.
 */
typedef struct forebay_port_argument {
    uint32_t size;
    const spisor_value *value;
    const char *settings_file;
    const forebay_port_failure *failure;
} forebay_port_argument;

/** The port and the address to listen on. First shape ends at `storage`. */
typedef struct forebay_port_answer {
    uint32_t size;
    uint32_t port;
    const char *address;
    void *storage;
} forebay_port_answer;

/** Why the proxy failed a request. Append-only: a published value keeps its number. */
typedef enum forebay_failure_reason {
    FOREBAY_UPSTREAM_UNREACHABLE = 1,
    FOREBAY_REQUEST_FAILED = 2
} forebay_failure_reason;

/** Why it failed, the `upstream` an unreachable one names, and the `detail`. First shape ends at `detail`. */
typedef struct forebay_failure_argument {
    uint32_t size;
    /** A forebay_failure_reason. */
    uint32_t reason;
    const char *upstream;
    const char *detail;
} forebay_failure_argument;

/** The reply to answer with and the lines to log. First shape ends at `storage`. */
typedef struct forebay_failure_answer {
    uint32_t size;
    /** A forebay_reply_kind. */
    uint32_t reply_kind;
    uint32_t status;
    const char *message;
    const forebay_log_line *log;
    size_t log_count;
    void *storage;
} forebay_failure_answer;

/**
 * Where an application's own login keeps its state: `name` in the directory `directory_variable` names, or home.
 * First shape ends at `member`.
 */
typedef struct forebay_state_file {
    uint32_t size;
    const char *directory_variable;
    const char *name;
    const char *member;
} forebay_state_file;

/**
 * How an application is pointed at the proxy: the variable naming its base URL, the placeholder credential it needs,
 * the variables of its own login and, NULL for none, its state file, and the variables naming its model, each NULL
 * when it has none. First shape ends at `model_variable`.
 */
typedef struct forebay_application {
    uint32_t size;
    const char *base_url_variable;
    const char *placeholder_variable;
    const char *placeholder_value;
    const char *const *own_login_variables;
    size_t own_login_variable_count;
    const forebay_state_file *state_file;
    const char *tier_model_variable;
    const char *tier_model_name_variable;
    const char *model_variable;
} forebay_application;

/**
 * The environment an application starts with, from the one it would have had, pointed at the proxy on `port`, from 1
 * to 65535. First shape ends at `application`.
 */
typedef struct forebay_environment_argument {
    uint32_t size;
    const forebay_slot *slots;
    size_t slot_count;
    uint32_t port;
    uint32_t providers_enabled;
    const forebay_pair *environment;
    size_t environment_count;
    const char *user_home;
    const forebay_application *application;
} forebay_environment_argument;

/** The whole environment to start the application with. First shape ends at `storage`. */
typedef struct forebay_environment_answer {
    uint32_t size;
    const forebay_pair *environment;
    size_t environment_count;
    void *storage;
} forebay_environment_answer;

/**
 * The routing service. Every operation answers MALFORMED, before it allocates, for an argument or an answer absent
 * or short of its first shape, an answer that is not empty, a required text that is NULL, an array NULL beside a
 * nonzero count, or an enum value the header does not name; REFUSED, with the refusal saying what to repair; and
 * OUT_OF_MEMORY, with the answer empty. First shape ends at `release_refusal`.
 */
typedef struct forebay_routing_definition {
    uint32_t size;
    void *context;
    /** REFUSED for a tier regex the matcher cannot run. */
    spisor_status (*tiers)(void *context, const forebay_tiers_argument *argument, forebay_tiers_answer *answer,
                           spisor_refusal *refusal);
    /** REFUSED for a pattern the matcher cannot run; MALFORMED for settings that are not a record. */
    spisor_status (*model_map)(void *context, const forebay_model_map_argument *argument,
                               forebay_model_map_answer *answer, spisor_refusal *refusal);
    /** REFUSED for a pattern the matcher cannot run. */
    spisor_status (*resolve)(void *context, const forebay_resolve_argument *argument, forebay_resolve_answer *answer,
                             spisor_refusal *refusal);
    spisor_status (*outcome)(void *context, const forebay_outcome_argument *argument, forebay_outcome_answer *answer,
                             spisor_refusal *refusal);
    /** The next thing to do for one request; REFUSED for a pattern the matcher cannot run. */
    spisor_status (*step)(void *context, const forebay_step_argument *argument, forebay_step_answer *answer,
                          spisor_refusal *refusal);
    /** The fixed limits; they read nothing. */
    spisor_status (*limits)(void *context, forebay_limits_answer *answer, spisor_refusal *refusal);
    /** REFUSED for an upstream or URL the request cannot be sent to. */
    spisor_status (*pass_through)(void *context, const forebay_pass_through_argument *argument,
                                  forebay_pass_through_answer *answer, spisor_refusal *refusal);
    spisor_status (*reply_headers)(void *context, const forebay_reply_headers_argument *argument,
                                   forebay_reply_headers_answer *answer, spisor_refusal *refusal);
    /** A request admitted only from the loopback address on the port, carrying no Origin. */
    spisor_status (*arrival)(void *context, const forebay_arrival_argument *argument, forebay_arrival_answer *answer,
                             spisor_refusal *refusal);
    /** REFUSED for a setting that is no port, or a port the proxy could not listen on, naming the settings file. */
    spisor_status (*port)(void *context, const forebay_port_argument *argument, forebay_port_answer *answer,
                          spisor_refusal *refusal);
    /** REFUSED for a failure routing has no reply for. */
    spisor_status (*failure)(void *context, const forebay_failure_argument *argument, forebay_failure_answer *answer,
                             spisor_refusal *refusal);
    spisor_status (*environment)(void *context, const forebay_environment_argument *argument,
                                 forebay_environment_answer *answer, spisor_refusal *refusal);
    void (*release_tiers_answer)(void *context, forebay_tiers_answer *answer);
    void (*release_model_map_answer)(void *context, forebay_model_map_answer *answer);
    void (*release_resolve_answer)(void *context, forebay_resolve_answer *answer);
    void (*release_step_answer)(void *context, forebay_step_answer *answer);
    void (*release_arrival_answer)(void *context, forebay_arrival_answer *answer);
    /** Frees a walk, NULL doing nothing; only after every step using it has returned, since a step reads it. */
    void (*release_walk)(void *context, forebay_walk *walk);
    void (*release_pass_through_answer)(void *context, forebay_pass_through_answer *answer);
    void (*release_reply_headers_answer)(void *context, forebay_reply_headers_answer *answer);
    void (*release_port_answer)(void *context, forebay_port_answer *answer);
    void (*release_failure_answer)(void *context, forebay_failure_answer *answer);
    void (*release_environment_answer)(void *context, forebay_environment_answer *answer);
    void (*release_refusal)(void *context, spisor_refusal *refusal);
    /**
     * Every model id the catalog serves, once as the first provider offers it, each with its name and limits filled
     * as forebay_model_entries_answer says; a profile short of `default_output` gives no defaults.
     */
    spisor_status (*model_entries)(void *context, const forebay_model_entries_argument *argument,
                                   forebay_model_entries_answer *answer, spisor_refusal *refusal);
    void (*release_model_entries_answer)(void *context, forebay_model_entries_answer *answer);
    /**
     * A new catalog with no round started, no answer kept and no model map, and its period timer due `limits`'
     * `refresh_every_milliseconds` after `now`. MALFORMED for a `now` that is not finite, a profile short of its tier
     * members and settings that are not a record.
     */
    spisor_status (*catalog_open)(void *context, const forebay_catalog_open_argument *argument,
                                  forebay_catalog_open_answer *answer, spisor_refusal *refusal);
    /**
     * What a host does about an event of its catalog, which keeps the rounds, so the host keeps only the asks, the
     * timers and the requests waiting. A round asks every implementation offered at its start within `limits`'
     * `models_budget_milliseconds`, the round close timer's, and closes once every ask settled or that timer fired,
     * whichever is first; its close derives the model map again over the answers kept, in its order, and releases
     * its waiting requests, and those of every round it superseded, in the same answer. A change always starts a new
     * round: a READ, a NO_MODEL or a period whose ordered list differs from the one the latest round asked, a
     * reordering included, since `model_entries` lists a model as the first provider offers it, asks a new round
     * whether or not one is open, and an open round it supersedes has its timer cleared and its requests wait for the
     * new one. Otherwise a READ waits while a round is open and reads now when none is; a period asks again unless a
     * round is open; and a NO_MODEL waits while a round is open, reads now while the latest round began less than
     * `limits`' `rederive_interval_milliseconds` ago, and asks again otherwise. A period re-sets its own timer. A new
     * round drops the answers of every implementation no longer offered. A failed ask keeps that implementation's
     * last models and warns `a provider answered no models: <message>`; an answer after its round closed derives the
     * map again, a failed one never. Only the latest round keeps answers and derives: an answer or a timer of an
     * older round is ignored, a failed answer of one still warned. A derivation `model_map` refuses keeps the last map
     * and warns `the model map could not be derived, so routing keeps the last one: <detail>`. MALFORMED, leaving the
     * catalog as it was, for a NULL `catalog`, a `now` that is not finite, a kind the header does not name, a list
     * holding a NULL or one implementation twice, a round the catalog never started, a period timer naming a round, an
     * answer of the latest round from an implementation whose ask it does not wait on, a failed answer without a
     * message and models that are no complete catalog provider. For an event answered anything but OK, which leaves
     * the catalog as it was, the host logs the refusal and does one more thing by its kind: a READ or NO_MODEL's
     * request reads the catalog as it stands through `catalog_read`; a TIMER answered OUT_OF_MEMORY is reported again
     * at once with a fresh `now`, until it is answered otherwise or the catalog is released, since nothing else closes
     * its round or re-sets the period, while a TIMER answered MALFORMED or REFUSED is the host's misuse, only logged
     * and never reported again; an ANSWERED is only logged, its round closing at its own round close timer, and no
     * other round ends.
     */
    spisor_status (*catalog_event)(void *context, const forebay_catalog_event_argument *argument,
                                   forebay_catalog_event_answer *answer, spisor_refusal *refusal);
    /** The catalog as it stands; MALFORMED for a NULL `catalog`. */
    spisor_status (*catalog_read)(void *context, const forebay_catalog_read_argument *argument,
                                  forebay_catalog_read_answer *answer, spisor_refusal *refusal);
    void (*release_catalog_open_answer)(void *context, forebay_catalog_open_answer *answer);
    void (*release_catalog_event_answer)(void *context, forebay_catalog_event_answer *answer);
    void (*release_catalog_read_answer)(void *context, forebay_catalog_read_answer *answer);
    /** Frees a catalog, NULL doing nothing; the host clears the timers it set. */
    void (*release_catalog)(void *context, forebay_catalog *catalog);
} forebay_routing_definition;

/** The static forebay_routing_definition, living until the library is unloaded. */
FOREBAY_DEFINITION_API const void *forebay_routing_definition_of(void);

#ifdef __cplusplus
}
#endif

#endif
