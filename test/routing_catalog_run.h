#ifndef FOREBAY_TEST_ROUTING_CATALOG_RUN_H
#define FOREBAY_TEST_ROUTING_CATALOG_RUN_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "routing_blocks.h"

/**
 * @file
 * A catalog vector run through the definition as one host would: `open` opens a catalog, each of `events` is reported
 * to it in order, an event `snapshot` reading it instead, and the catalog is released. The result is {open, events}:
 * the open answer's timers, and one answer per event, written as the vector expects them.
 *
 * An event is {event, now, implementations, round, implementation, models, failed, message, timer}: `event` one of
 * read, noModel, answered, timer and snapshot; `models` one provider's catalog entry, keyed by its provider name;
 * `timer` roundClose or period.
 */

static inline cJSON *routing_catalog_refusal_json(spisor_status status, const spisor_refusal *refusal) {
    cJSON *result = cJSON_CreateObject();
    if (status == SPISOR_MALFORMED || status == SPISOR_REFUSED) {
        cJSON_AddStringToObject(result, status == SPISOR_REFUSED ? "refused" : "message",
                                refusal->detail == NULL ? "" : refusal->detail);
        cJSON_AddStringToObject(result, "fix", refusal->fix == NULL ? "" : refusal->fix);
    } else {
        cJSON_AddNumberToObject(result, "failedWithStatus", status);
    }
    return result;
}

static inline cJSON *routing_catalog_timers_json(const forebay_catalog_timer *timers, size_t count) {
    static const char *const kinds[] = {"(none)", "roundClose", "period"};
    cJSON *array = cJSON_CreateArray();
    for (size_t index = 0; index < count; index++) {
        const forebay_catalog_timer *timer = &timers[index];
        cJSON *json = cJSON_CreateObject();
        const char *kind = timer->kind <= 2 ? kinds[timer->kind] : "(unknown)";
        cJSON_AddStringToObject(json, timer->change == FOREBAY_CATALOG_SET ? "set" : "clear", kind);
        cJSON_AddNumberToObject(json, "round", (double)timer->round);
        if (timer->change == FOREBAY_CATALOG_SET) {
            cJSON_AddNumberToObject(json, "due", timer->due);
        }
        cJSON_AddItemToArray(array, json);
    }
    return array;
}

static inline cJSON *routing_catalog_rounds_json(const uint64_t *rounds, size_t count) {
    cJSON *array = cJSON_CreateArray();
    for (size_t index = 0; index < count; index++) {
        cJSON_AddItemToArray(array, cJSON_CreateNumber((double)rounds[index]));
    }
    return array;
}

/** An event answer with only what it carries: an empty answer is {}. */
static inline cJSON *routing_catalog_event_json(const forebay_catalog_event_answer *answer) {
    cJSON *result = cJSON_CreateObject();
    if (answer->ask_round != 0) {
        cJSON *ask = cJSON_CreateObject();
        cJSON_AddNumberToObject(ask, "round", (double)answer->ask_round);
        cJSON_AddItemToObject(ask, "implementations", routing_texts_json_of(answer->asks, answer->ask_count));
        cJSON_AddNumberToObject(ask, "budgetMilliseconds", (double)answer->budget_milliseconds);
        cJSON_AddItemToObject(result, "ask", ask);
    }
    if (answer->timer_count > 0) {
        cJSON_AddItemToObject(result, "timers", routing_catalog_timers_json(answer->timers, answer->timer_count));
    }
    if (answer->wait_round != 0) {
        cJSON_AddNumberToObject(result, "wait", (double)answer->wait_round);
    }
    if (answer->released_count > 0) {
        cJSON_AddItemToObject(result, "released",
                              routing_catalog_rounds_json(answer->released, answer->released_count));
    }
    if (answer->log_count > 0) {
        cJSON_AddItemToObject(result, "log", routing_log_json_of(answer->log, answer->log_count));
    }
    return result;
}

/** The snapshot as {implementations, providers, map, entries}, each slot as its chain of `provider/model`. */
static inline cJSON *routing_catalog_read_json(const forebay_catalog_read_answer *answer) {
    cJSON *result = cJSON_CreateObject();
    cJSON *providers = cJSON_CreateArray();
    cJSON *map = cJSON_CreateObject();
    cJSON *entries = cJSON_CreateArray();
    for (size_t index = 0; index < answer->provider_count; index++) {
        cJSON_AddItemToArray(providers, cJSON_CreateString(answer->providers[index].provider));
    }
    for (size_t index = 0; index < answer->slot_count; index++) {
        cJSON *chain = cJSON_CreateArray();
        for (size_t entry = 0; entry < answer->slots[index].count; entry++) {
            const forebay_assignment *assignment = &answer->slots[index].chain[entry];
            char named[256];
            snprintf(named, sizeof named, "%s/%s", assignment->provider, assignment->model);
            cJSON_AddItemToArray(chain, cJSON_CreateString(named));
        }
        cJSON_AddItemToObject(map, answer->slots[index].slot, chain);
    }
    for (size_t index = 0; index < answer->entry_count; index++) {
        cJSON_AddItemToArray(entries, cJSON_CreateString(answer->entries[index].id));
    }
    cJSON_AddItemToObject(result, "implementations",
                          routing_texts_json_of(answer->implementations, answer->implementation_count));
    cJSON_AddItemToObject(result, "providers", providers);
    cJSON_AddItemToObject(result, "map", map);
    cJSON_AddItemToObject(result, "entries", entries);
    return result;
}

static inline uint32_t routing_catalog_kind_named(const char *name, const char *const *names, uint32_t count) {
    for (uint32_t index = 0; name != NULL && index < count; index++) {
        if (strcmp(names[index], name) == 0) {
            return index + 1;
        }
    }
    return 0;
}

/** Reads an event of the vector into `argument`, its arrays from the vector's blocks. */
static inline void routing_catalog_event_of(const cJSON *json, forebay_catalog_event_argument *argument) {
    static const char *const kinds[] = {"read", "noModel", "answered", "timer"};
    static const char *const timers[] = {"roundClose", "period"};
    const cJSON *implementations = cJSON_GetObjectItemCaseSensitive(json, "implementations");
    const cJSON *models = cJSON_GetObjectItemCaseSensitive(json, "models");
    argument->kind = routing_catalog_kind_named(routing_text_of(json, "event"), kinds, 4);
    argument->now = cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(json, "now"));
    argument->implementations = routing_texts_of(implementations, &argument->implementation_count);
    argument->round = routing_count_of(json, "round");
    argument->implementation = routing_text_of(json, "implementation");
    if (models != NULL) {
        size_t count = 0;
        argument->models = routing_catalog_of(models, &count);
    }
    argument->failed = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "failed")) ? 1u : 0u;
    argument->message = routing_text_of(json, "message");
    argument->timer = routing_catalog_kind_named(routing_text_of(json, "timer"), timers, 2);
}

/** Zeroes an answer whole, padding included, and sets its size, so a non-OK call's answer can be read byte by byte. */
static inline void routing_catalog_empty(void *answer, size_t size) {
    uint32_t sized = (uint32_t)size;
    memset(answer, 0, size);
    memcpy(answer, &sized, sizeof sized);
}

/** Whether `bytes` after a struct's size are all zero, as a non-OK call leaves its answer. */
static inline int routing_catalog_is_empty(const void *answer, size_t size) {
    const unsigned char *bytes = answer;
    for (size_t index = sizeof(uint32_t); index < size; index++) {
        if (bytes[index] != 0) {
            return 0;
        }
    }
    return 1;
}

/**
 * Arms memory failure around the definition's calls alone, so the run's own JSON is never what runs out: `arm` before
 * the call of event `event`, from 0, or SIZE_MAX for the open, and `disarm` after it. A call answering OUT_OF_MEMORY
 * under a guard is made again unarmed, which a catalog left as it was answers as it would have.
 */
typedef struct routing_catalog_guard {
    void (*arm)(void *data, size_t event);
    void (*disarm)(void *data);
    void *data;
} routing_catalog_guard;

static inline void routing_catalog_arm(const routing_catalog_guard *guard, size_t event) {
    if (guard != NULL) {
        guard->arm(guard->data, event);
    }
}

static inline void routing_catalog_disarm(const routing_catalog_guard *guard) {
    if (guard != NULL) {
        guard->disarm(guard->data);
    }
}

static inline int routing_catalog_is_snapshot(const cJSON *event) {
    const char *name = routing_text_of(event, "event");
    return name != NULL && strcmp(name, "snapshot") == 0;
}

/** One event `index`, its answer as the vector writes it; NULL when memory ran out with no guard to call again. */
static inline cJSON *routing_catalog_report(forebay_catalog *catalog, const cJSON *json, size_t index,
                                            const routing_catalog_guard *guard) {
    const forebay_routing_definition *routing = forebay_routing_definition_of();
    forebay_catalog_event_argument argument = SPISOR_INIT(forebay_catalog_event_argument);
    forebay_catalog_event_answer answer;
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    routing_catalog_empty(&answer, sizeof answer);
    routing_catalog_event_of(json, &argument);
    argument.catalog = catalog;
    routing_catalog_arm(guard, index);
    spisor_status status = routing->catalog_event(routing->context, &argument, &answer, &refusal);
    routing_catalog_disarm(guard);
    int left_empty = status == SPISOR_OK || routing_catalog_is_empty(&answer, sizeof answer);
    if (status == SPISOR_OUT_OF_MEMORY && guard != NULL && left_empty) {
        status = routing->catalog_event(routing->context, &argument, &answer, &refusal);
    }
    if (status == SPISOR_OUT_OF_MEMORY && guard == NULL) {
        return NULL;
    }
    cJSON *result =
        status == SPISOR_OK ? routing_catalog_event_json(&answer) : routing_catalog_refusal_json(status, &refusal);
    if (!left_empty) {
        cJSON_AddBoolToObject(result, "answerNotEmpty", 1);
    }
    routing->release_catalog_event_answer(routing->context, &answer);
    routing->release_refusal(routing->context, &refusal);
    return result;
}

static inline cJSON *routing_catalog_snapshot(const forebay_catalog *catalog, size_t index,
                                              const routing_catalog_guard *guard) {
    const forebay_routing_definition *routing = forebay_routing_definition_of();
    forebay_catalog_read_argument argument = SPISOR_INIT(forebay_catalog_read_argument);
    forebay_catalog_read_answer answer;
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    routing_catalog_empty(&answer, sizeof answer);
    argument.catalog = catalog;
    routing_catalog_arm(guard, index);
    spisor_status status = routing->catalog_read(routing->context, &argument, &answer, &refusal);
    routing_catalog_disarm(guard);
    int left_empty = status == SPISOR_OK || routing_catalog_is_empty(&answer, sizeof answer);
    if (status == SPISOR_OUT_OF_MEMORY && guard != NULL && left_empty) {
        status = routing->catalog_read(routing->context, &argument, &answer, &refusal);
    }
    if (status == SPISOR_OUT_OF_MEMORY && guard == NULL) {
        return NULL;
    }
    cJSON *result =
        status == SPISOR_OK ? routing_catalog_read_json(&answer) : routing_catalog_refusal_json(status, &refusal);
    if (!left_empty) {
        cJSON_AddBoolToObject(result, "answerNotEmpty", 1);
    }
    routing->release_catalog_read_answer(routing->context, &answer);
    routing->release_refusal(routing->context, &refusal);
    return result;
}

static inline spisor_status routing_catalog_opened(const forebay_catalog_open_argument *open,
                                                   forebay_catalog_open_answer *opened, spisor_refusal *refusal,
                                                   const routing_catalog_guard *guard) {
    const forebay_routing_definition *routing = forebay_routing_definition_of();
    routing_catalog_arm(guard, SIZE_MAX);
    spisor_status status = routing->catalog_open(routing->context, open, opened, refusal);
    routing_catalog_disarm(guard);
    if (status == SPISOR_OUT_OF_MEMORY && guard != NULL && routing_catalog_is_empty(opened, sizeof *opened)) {
        status = routing->catalog_open(routing->context, open, opened, refusal);
    }
    return status;
}

/**
 * Runs the vector's catalog: {open, events} on OK, NULL with `*status` OUT_OF_MEMORY when memory ran out with no
 * guard, the catalog released either way.
 */
static inline cJSON *routing_catalog_run(const forebay_catalog_open_argument *open, const cJSON *events,
                                         const routing_catalog_guard *guard, spisor_status *status) {
    const forebay_routing_definition *routing = forebay_routing_definition_of();
    forebay_catalog_open_answer opened;
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    routing_catalog_empty(&opened, sizeof opened);
    *status = routing_catalog_opened(open, &opened, &refusal, guard);
    if (*status != SPISOR_OK) {
        cJSON *refused = NULL;
        if (*status != SPISOR_OUT_OF_MEMORY) {
            refused = cJSON_CreateObject();
            cJSON_AddItemToObject(refused, "open", routing_catalog_refusal_json(*status, &refusal));
            *status = SPISOR_OK;
        }
        routing->release_refusal(routing->context, &refusal);
        return refused;
    }
    cJSON *result = cJSON_CreateObject();
    cJSON *answers = cJSON_CreateArray();
    cJSON_AddItemToObject(result, "open", routing_catalog_timers_json(opened.timers, opened.timer_count));
    cJSON_AddItemToObject(result, "events", answers);
    const cJSON *event = NULL;
    size_t index = 0;
    cJSON_ArrayForEach(event, events) {
        cJSON *answer = routing_catalog_is_snapshot(event) ? routing_catalog_snapshot(opened.catalog, index, guard)
                                                           : routing_catalog_report(opened.catalog, event, index, guard);
        index++;
        if (answer == NULL) {
            *status = SPISOR_OUT_OF_MEMORY;
            cJSON_Delete(result);
            result = NULL;
            break;
        }
        cJSON_AddItemToArray(answers, answer);
    }
    routing->release_catalog(routing->context, opened.catalog);
    routing->release_catalog_open_answer(routing->context, &opened);
    return result;
}

#endif
