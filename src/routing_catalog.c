#include "routing_catalog.h"

#include <string.h>

#include "answer_storage.h"
#include "definition_support.h"
#include "routing_arguments.h"
#include "routing_catalog_state.h"
#include "routing_limits.h"
#include "spisor/allocation.h"

#define OPEN_SHAPE SPISOR_SHAPE_THROUGH(forebay_catalog_open_argument, settings)
#define OPEN_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_catalog_open_answer, storage)
#define EVENT_SHAPE SPISOR_SHAPE_THROUGH(forebay_catalog_event_argument, timer)
#define EVENT_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_catalog_event_answer, storage)
#define READ_SHAPE SPISOR_SHAPE_THROUGH(forebay_catalog_read_argument, catalog)
#define READ_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_catalog_read_answer, storage)
#define PROFILE_DEFAULTS_SHAPE SPISOR_SHAPE_THROUGH(forebay_routing_profile, default_output)

#define CATALOG_RULE "a catalog catalog_open answered"

static const char NO_MODELS_WARNING[] = "a provider answered no models: ";

void routing_release_catalog(void *context, forebay_catalog *catalog) {
    (void)context;
    if (catalog == NULL) {
        return;
    }
    while (catalog->kept != NULL) {
        catalog_kept *next = catalog->kept->next;
        routing_catalog_free_kept(catalog->kept);
        catalog->kept = next;
    }
    routing_catalog_free_round(catalog->round);
    cJSON_Delete(catalog->map);
    cJSON_Delete(catalog->profile);
    cJSON_Delete(catalog->settings);
    spisor_memory_release(catalog);
}

/** The profile members a read's entries need: its size, through its defaults at most, and the defaults it reaches. */
static forebay_routing_profile defaults_of(const forebay_routing_profile *profile) {
    forebay_routing_profile defaults = SPISOR_INIT(forebay_routing_profile);
    if (spisor_has_shape(profile->size, PROFILE_DEFAULTS_SHAPE)) {
        defaults.default_context = profile->default_context;
        defaults.default_output = profile->default_output;
    } else {
        defaults.size = (uint32_t)offsetof(forebay_routing_profile, default_context);
    }
    return defaults;
}

static int is_open_argument(const forebay_catalog_open_argument *argument) {
    return forebay_is_finite(argument->now) && routing_is_profile(argument->profile, 1) &&
           argument->settings != NULL && argument->settings->kind == SPISOR_VALUE_RECORD;
}

static int fill_period(double now, forebay_catalog_open_answer *answer) {
    forebay_limits_answer limits = SPISOR_INIT(forebay_limits_answer);
    forebay_catalog_timer *timer = forebay_storage_array(&answer->storage, 1, sizeof *timer);
    if (timer == NULL) {
        return 0;
    }
    routing_read_limits(&limits);
    timer->change = FOREBAY_CATALOG_SET;
    timer->kind = FOREBAY_CATALOG_PERIOD;
    timer->due = now + (double)limits.refresh_every_milliseconds;
    answer->timers = timer;
    answer->timer_count = 1;
    return 1;
}

spisor_status routing_catalog_open(void *context, const forebay_catalog_open_argument *argument,
                                   forebay_catalog_open_answer *answer, spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, OPEN_SHAPE) ||
        !SPISOR_IS_EMPTY_COUNTED_ANSWER(answer, OPEN_ANSWER_SHAPE, answer->timer_count) || answer->catalog != NULL) {
        return spisor_refuse_shape(refusal, "routing's catalog_open");
    }
    if (!is_open_argument(argument)) {
        return forebay_refuse_member(refusal, "now, the profile or settings",
                                     "a finite now, a profile with its tier members and a settings record");
    }
    forebay_catalog *catalog = spisor_memory_allocate_zeroed(1, sizeof *catalog);
    if (catalog == NULL) {
        return SPISOR_OUT_OF_MEMORY;
    }
    catalog->defaults = defaults_of(argument->profile);
    catalog->profile = routing_profile_json(argument->profile);
    spisor_status status = forebay_json_of_member(argument->settings, "settings", &catalog->settings, refusal);
    if (status == SPISOR_OK && (catalog->profile == NULL || !fill_period(argument->now, answer))) {
        status = SPISOR_OUT_OF_MEMORY;
    }
    if (status != SPISOR_OK) {
        routing_release_catalog(NULL, catalog);
        forebay_storage_release(answer->storage);
        SPISOR_CLEAR_ANSWER(answer, OPEN_ANSWER_SHAPE);
        return status;
    }
    answer->catalog = catalog;
    return SPISOR_OK;
}

/** Whether `count` texts at a present array are each set and none of them twice. */
static int is_list(const char *const *texts, size_t count) {
    if (!routing_are_texts(texts, count)) {
        return 0;
    }
    for (size_t index = 0; index < count; index++) {
        for (size_t earlier = 0; earlier < index; earlier++) {
            if (strcmp(texts[earlier], texts[index]) == 0) {
                return 0;
            }
        }
    }
    return 1;
}

static int is_kind(const forebay_catalog_event_argument *event) {
    return event->kind >= FOREBAY_CATALOG_READ && event->kind <= FOREBAY_CATALOG_TIMER &&
           forebay_is_finite(event->now);
}

static spisor_status check_answered(const forebay_catalog_event_argument *event, spisor_refusal *refusal) {
    if (event->implementation == NULL) {
        return forebay_refuse_member(refusal, "implementation", "an implementation whose ask the round awaits");
    }
    if (event->failed && event->message == NULL) {
        return forebay_refuse_member(refusal, "message", "the failure's text, for a failed answer");
    }
    if (!event->failed && (event->models == NULL || !routing_is_catalog(event->models, 1))) {
        return forebay_refuse_member(refusal, "models", "a complete catalog provider, for an answer that did not fail");
    }
    return SPISOR_OK;
}

static spisor_status check_event(const forebay_catalog_event_argument *event, spisor_refusal *refusal) {
    if (event->catalog == NULL) {
        return forebay_refuse_member(refusal, "catalog", CATALOG_RULE);
    }
    if (!is_kind(event)) {
        return forebay_refuse_member(refusal, "kind or now", "a forebay_catalog_event_kind at a finite now");
    }
    if (!is_list(event->implementations, event->implementation_count)) {
        return forebay_refuse_member(refusal, "implementations", "a list naming each implementation once");
    }
    if (event->kind == FOREBAY_CATALOG_ANSWERED) {
        return check_answered(event, refusal);
    }
    if (event->kind == FOREBAY_CATALOG_TIMER && event->timer != FOREBAY_CATALOG_ROUND_CLOSE &&
        event->timer != FOREBAY_CATALOG_PERIOD) {
        return forebay_refuse_member(refusal, "timer", "a forebay_catalog_timer_kind");
    }
    return SPISOR_OK;
}

/** Allocates all the event needs, the log lines included; OUT_OF_MEMORY with what it took left in `prepared`. */
static spisor_status prepare(const forebay_catalog *catalog, const catalog_plan *plan,
                             const forebay_catalog_event_argument *event, catalog_prepared *prepared, cJSON *log) {
    uint64_t next = catalog->round == NULL ? 1 : catalog->round->id + 1;
    if (log == NULL || (plan->warns_failure && !routing_catalog_warn(log, NO_MODELS_WARNING, event->message))) {
        return SPISOR_OUT_OF_MEMORY;
    }
    if (plan->starts) {
        prepared->round =
            routing_catalog_new_round(next, event->now, event->implementations, event->implementation_count);
        if (prepared->round == NULL) {
            return SPISOR_OUT_OF_MEMORY;
        }
    }
    if (plan->stores) {
        prepared->kept = routing_catalog_new_kept(event->implementation, event->models);
        if (prepared->kept == NULL) {
            return SPISOR_OUT_OF_MEMORY;
        }
    }
    if (!plan->derives) {
        return SPISOR_OK;
    }
    return routing_catalog_derive(catalog, routing_catalog_acted_round(catalog, prepared), prepared->kept,
                                  &prepared->map, log);
}

static void discard(catalog_prepared *prepared) {
    routing_catalog_free_round(prepared->round);
    routing_catalog_free_kept(prepared->kept);
    cJSON_Delete(prepared->map);
}

static int is_empty_event_answer(const forebay_catalog_event_answer *answer) {
    return SPISOR_IS_EMPTY_COUNTED_ANSWER(answer, EVENT_ANSWER_SHAPE,
                                          answer->ask_count | answer->timer_count | answer->released_count |
                                              answer->log_count);
}

spisor_status routing_catalog_event(void *context, const forebay_catalog_event_argument *argument,
                                    forebay_catalog_event_answer *answer, spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, EVENT_SHAPE) || !is_empty_event_answer(answer)) {
        return spisor_refuse_shape(refusal, "routing's catalog_event");
    }
    catalog_plan plan;
    spisor_status status = check_event(argument, refusal);
    if (status == SPISOR_OK) {
        status = routing_catalog_decide(argument->catalog, argument, &plan, refusal);
    }
    if (status != SPISOR_OK) {
        return status;
    }
    catalog_prepared prepared = {NULL, NULL, NULL};
    cJSON *log = cJSON_CreateArray();
    status = prepare(argument->catalog, &plan, argument, &prepared, log);
    if (status == SPISOR_OK &&
        !routing_catalog_fill_answer(argument->catalog, &plan, argument, &prepared, log, answer)) {
        status = SPISOR_OUT_OF_MEMORY;
    }
    if (status == SPISOR_OK) {
        routing_catalog_commit(argument->catalog, &plan, &prepared);
    } else {
        forebay_storage_release(answer->storage);
        SPISOR_CLEAR_ANSWER(answer, EVENT_ANSWER_SHAPE);
    }
    discard(&prepared);
    cJSON_Delete(log);
    return status;
}

spisor_status routing_catalog_read(void *context, const forebay_catalog_read_argument *argument,
                                   forebay_catalog_read_answer *answer, spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, READ_SHAPE) ||
        !SPISOR_IS_EMPTY_COUNTED_ANSWER(answer, READ_ANSWER_SHAPE,
                                        answer->implementation_count | answer->provider_count | answer->slot_count |
                                            answer->entry_count)) {
        return spisor_refuse_shape(refusal, "routing's catalog_read");
    }
    if (argument->catalog == NULL) {
        return forebay_refuse_member(refusal, "catalog", CATALOG_RULE);
    }
    if (!routing_catalog_fill_read(argument->catalog, answer)) {
        forebay_storage_release(answer->storage);
        SPISOR_CLEAR_ANSWER(answer, READ_ANSWER_SHAPE);
        return SPISOR_OUT_OF_MEMORY;
    }
    return SPISOR_OK;
}

#define RELEASE_ANSWER(name, type, shape)                              \
    void name(void *context, type *answer) {                           \
        (void)context;                                                 \
        if (answer != NULL && spisor_has_shape(answer->size, shape)) { \
            forebay_storage_release(answer->storage);                  \
            SPISOR_CLEAR_ANSWER(answer, shape);                        \
        }                                                              \
    }

RELEASE_ANSWER(routing_release_catalog_open_answer, forebay_catalog_open_answer, OPEN_ANSWER_SHAPE)
RELEASE_ANSWER(routing_release_catalog_event_answer, forebay_catalog_event_answer, EVENT_ANSWER_SHAPE)
RELEASE_ANSWER(routing_release_catalog_read_answer, forebay_catalog_read_answer, READ_ANSWER_SHAPE)
