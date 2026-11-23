#include <string.h>

#include "answer_storage.h"
#include "routing_answers.h"
#include "routing_catalog_state.h"
#include "routing_limits.h"
#include "routing_model_entries.h"

#define MOST_TIMERS 3

const catalog_round *routing_catalog_acted_round(const forebay_catalog *catalog, const catalog_prepared *prepared) {
    return prepared->round != NULL ? prepared->round : catalog->round;
}

/** The first round whose requests wait on the acted round once the plan superseded what it supersedes, 0 for none. */
static uint64_t first_waiting(const forebay_catalog *catalog, const catalog_plan *plan) {
    if (catalog->first_waiting != 0 || !plan->supersedes) {
        return catalog->first_waiting;
    }
    return catalog->round->id;
}

static void add_timer(forebay_catalog_timer *timers, size_t *count, uint32_t change, uint32_t kind, uint64_t round,
                      double due) {
    forebay_catalog_timer *timer = &timers[(*count)++];
    timer->change = change;
    timer->kind = kind;
    timer->round = round;
    timer->due = due;
}

static int fill_timers(const forebay_catalog *catalog, const catalog_plan *plan, const catalog_round *acted,
                       double now, forebay_catalog_event_answer *answer) {
    forebay_limits_answer limits = SPISOR_INIT(forebay_limits_answer);
    forebay_catalog_timer timers[MOST_TIMERS];
    size_t count = 0;
    routing_read_limits(&limits);
    if (plan->supersedes) {
        add_timer(timers, &count, FOREBAY_CATALOG_CLEAR, FOREBAY_CATALOG_ROUND_CLOSE, catalog->round->id, 0);
    }
    if (plan->clears_close_timer) {
        add_timer(timers, &count, FOREBAY_CATALOG_CLEAR, FOREBAY_CATALOG_ROUND_CLOSE, acted->id, 0);
    }
    if (plan->starts && !plan->closes) {
        add_timer(timers, &count, FOREBAY_CATALOG_SET, FOREBAY_CATALOG_ROUND_CLOSE, acted->id,
                  now + (double)limits.models_budget_milliseconds);
    }
    if (plan->sets_period) {
        add_timer(timers, &count, FOREBAY_CATALOG_SET, FOREBAY_CATALOG_PERIOD, 0,
                  now + (double)limits.refresh_every_milliseconds);
    }
    forebay_catalog_timer *filled = forebay_storage_array(&answer->storage, count, sizeof *filled);
    if (count > 0 && filled == NULL) {
        return 0;
    }
    if (count > 0) {
        memcpy(filled, timers, count * sizeof *filled);
    }
    answer->timers = filled;
    answer->timer_count = count;
    return 1;
}

static int fill_asks(const catalog_round *acted, forebay_catalog_event_answer *answer) {
    forebay_limits_answer limits = SPISOR_INIT(forebay_limits_answer);
    const char **asks = forebay_storage_array(&answer->storage, acted->count, sizeof *asks);
    if (acted->count > 0 && asks == NULL) {
        return 0;
    }
    for (size_t index = 0; index < acted->count; index++) {
        if (!forebay_storage_text(&answer->storage, acted->implementations[index], &asks[index])) {
            return 0;
        }
    }
    routing_read_limits(&limits);
    answer->ask_round = acted->id;
    answer->asks = asks;
    answer->ask_count = acted->count;
    answer->budget_milliseconds = limits.models_budget_milliseconds;
    return 1;
}

static int fill_released(uint64_t first, uint64_t last, forebay_catalog_event_answer *answer) {
    size_t count = (size_t)(last - first + 1);
    uint64_t *released = forebay_storage_array(&answer->storage, count, sizeof *released);
    if (released == NULL) {
        return 0;
    }
    for (size_t index = 0; index < count; index++) {
        released[index] = first + index;
    }
    answer->released = released;
    answer->released_count = count;
    return 1;
}

int routing_catalog_fill_answer(const forebay_catalog *catalog, const catalog_plan *plan,
                                const forebay_catalog_event_argument *event, const catalog_prepared *prepared,
                                const cJSON *log, forebay_catalog_event_answer *answer) {
    const catalog_round *acted = routing_catalog_acted_round(catalog, prepared);
    uint64_t waiting = first_waiting(catalog, plan);
    answer->wait_round = plan->waits ? acted->id : 0;
    return (!plan->starts || fill_asks(acted, answer)) && fill_timers(catalog, plan, acted, event->now, answer) &&
           (!plan->closes || fill_released(waiting != 0 ? waiting : acted->id, acted->id, answer)) &&
           routing_read_log(&answer->storage, log, &answer->log, &answer->log_count);
}

static int is_offered(const catalog_round *round, const char *implementation) {
    for (size_t index = 0; index < round->count; index++) {
        if (strcmp(round->implementations[index], implementation) == 0) {
            return 1;
        }
    }
    return 0;
}

/** Frees every kept answer of an implementation `round` does not offer. */
static void drop_unoffered(forebay_catalog *catalog, const catalog_round *round) {
    catalog_kept **link = &catalog->kept;
    while (*link != NULL) {
        catalog_kept *kept = *link;
        if (is_offered(round, kept->implementation)) {
            link = &kept->next;
        } else {
            *link = kept->next;
            routing_catalog_free_kept(kept);
        }
    }
}

static void keep(forebay_catalog *catalog, catalog_kept *fresh) {
    catalog_kept **link = &catalog->kept;
    while (*link != NULL && strcmp((*link)->implementation, fresh->implementation) != 0) {
        link = &(*link)->next;
    }
    if (*link != NULL) {
        catalog_kept *replaced = *link;
        *link = replaced->next;
        routing_catalog_free_kept(replaced);
    }
    fresh->next = catalog->kept;
    catalog->kept = fresh;
}

void routing_catalog_commit(forebay_catalog *catalog, const catalog_plan *plan, catalog_prepared *prepared) {
    if (plan->starts) {
        catalog->first_waiting = first_waiting(catalog, plan);
        routing_catalog_free_round(catalog->round);
        catalog->round = prepared->round;
        prepared->round = NULL;
        drop_unoffered(catalog, catalog->round);
    }
    if (plan->settles) {
        catalog->round->settled[plan->settled] = 1;
    }
    if (plan->stores) {
        keep(catalog, prepared->kept);
        prepared->kept = NULL;
    }
    if (prepared->map != NULL) {
        cJSON_Delete(catalog->map);
        catalog->map = prepared->map;
        prepared->map = NULL;
    }
    if (plan->closes) {
        catalog->round->closed = 1;
        catalog->first_waiting = 0;
    }
}

static int fill_read_providers(const forebay_catalog *catalog, forebay_catalog_read_answer *answer) {
    const catalog_round *round = catalog->round;
    size_t total = round == NULL ? 0 : round->count;
    const char **implementations = forebay_storage_array(&answer->storage, total, sizeof *implementations);
    forebay_catalog_provider *providers = forebay_storage_array(&answer->storage, total, sizeof *providers);
    size_t count = 0;
    if (total > 0 && (implementations == NULL || providers == NULL)) {
        return 0;
    }
    for (size_t index = 0; index < total; index++) {
        const catalog_kept *kept = routing_catalog_kept_of(catalog, NULL, round->implementations[index]);
        if (kept == NULL) {
            continue;
        }
        if (!forebay_storage_text(&answer->storage, kept->implementation, &implementations[count]) ||
            !routing_catalog_copy_provider(&answer->storage, &kept->provider, &providers[count])) {
            return 0;
        }
        count++;
    }
    answer->implementations = implementations;
    answer->implementation_count = count;
    answer->providers = providers;
    answer->provider_count = count;
    return 1;
}

int routing_catalog_fill_read(const forebay_catalog *catalog, forebay_catalog_read_answer *answer) {
    return fill_read_providers(catalog, answer) &&
           routing_read_slots(&answer->storage, catalog->map, &answer->slots, &answer->slot_count) &&
           routing_fill_model_entries(&answer->storage, &catalog->defaults, answer->providers, answer->provider_count,
                                      &answer->entries, &answer->entry_count);
}
