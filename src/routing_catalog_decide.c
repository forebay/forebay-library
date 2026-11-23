#include <string.h>

#include "definition_support.h"
#include "routing_catalog_state.h"
#include "routing_limits.h"

#define ROUND_RULE "a round the catalog started, or 0 for a period timer"
#define AWAITED_RULE "an implementation whose ask the round awaits"

static int is_open(const catalog_round *round) {
    return round != NULL && !round->closed;
}

/** Whether the event's ordered list differs from the one the latest round asked, none before the first round. */
static int is_changed(const catalog_round *round, const forebay_catalog_event_argument *event) {
    size_t count = round == NULL ? 0 : round->count;
    if (count != event->implementation_count) {
        return 1;
    }
    for (size_t index = 0; index < count; index++) {
        if (strcmp(round->implementations[index], event->implementations[index]) != 0) {
            return 1;
        }
    }
    return 0;
}

static int is_within_the_interval(const catalog_round *round, double now) {
    forebay_limits_answer limits = SPISOR_INIT(forebay_limits_answer);
    routing_read_limits(&limits);
    return round != NULL && now - round->started < (double)limits.rederive_interval_milliseconds;
}

static int is_request(const forebay_catalog_event_argument *event) {
    return event->kind == FOREBAY_CATALOG_READ || event->kind == FOREBAY_CATALOG_NO_MODEL;
}

/** Whether an unchanged list asks a new round: a period or a NO_MODEL past the interval, with no round open. */
static int asks_again(const catalog_round *round, const forebay_catalog_event_argument *event) {
    if (is_open(round) || event->kind == FOREBAY_CATALOG_READ) {
        return 0;
    }
    return event->kind != FOREBAY_CATALOG_NO_MODEL || !is_within_the_interval(round, event->now);
}

static void start_round(const catalog_round *round, const forebay_catalog_event_argument *event, catalog_plan *plan) {
    plan->starts = 1;
    plan->supersedes = is_open(round);
    if (event->implementation_count == 0) {
        plan->closes = 1;
        plan->derives = 1;
    } else {
        plan->waits = is_request(event);
    }
}

/** A READ, a NO_MODEL or a period: a change starts a round, an open round is waited on, or one is asked again. */
static void decide_request(const forebay_catalog *catalog, const forebay_catalog_event_argument *event,
                           catalog_plan *plan) {
    const catalog_round *round = catalog->round;
    if (is_changed(round, event) || asks_again(round, event)) {
        start_round(round, event, plan);
    } else {
        plan->waits = is_open(round) && is_request(event);
    }
    plan->sets_period = event->kind == FOREBAY_CATALOG_TIMER;
}

static int is_started(const catalog_round *round, uint64_t id) {
    return id != 0 && round != NULL && id <= round->id;
}

/** The index of `implementation` among the round's asks still waited on, or the round's count for none. */
static size_t awaited_index(const catalog_round *round, const char *implementation) {
    for (size_t index = 0; index < round->count; index++) {
        if (!round->settled[index] && strcmp(round->implementations[index], implementation) == 0) {
            return index;
        }
    }
    return round->count;
}

static int is_last_awaited(const catalog_round *round, size_t settled) {
    for (size_t index = 0; index < round->count; index++) {
        if (index != settled && !round->settled[index]) {
            return 0;
        }
    }
    return 1;
}

/**
 * @implNote An answer of an older round is only warned: for an implementation the latest round also asks, that
 * round's own answer is the one that counts, and a derivation over the older list would drop the newer providers.
 */
static spisor_status decide_answered(const catalog_round *round, const forebay_catalog_event_argument *event,
                                     catalog_plan *plan, spisor_refusal *refusal) {
    if (!is_started(round, event->round)) {
        return forebay_refuse_member(refusal, "round", ROUND_RULE);
    }
    plan->warns_failure = event->failed != 0;
    if (event->round < round->id) {
        return SPISOR_OK;
    }
    size_t index = awaited_index(round, event->implementation);
    if (index == round->count) {
        return forebay_refuse_member(refusal, "implementation", AWAITED_RULE);
    }
    plan->settles = 1;
    plan->settled = index;
    plan->stores = !event->failed;
    if (round->closed) {
        plan->derives = !event->failed;
    } else if (is_last_awaited(round, index)) {
        plan->closes = 1;
        plan->clears_close_timer = 1;
        plan->derives = 1;
    }
    return SPISOR_OK;
}

static spisor_status decide_timer(const forebay_catalog *catalog, const forebay_catalog_event_argument *event,
                                  catalog_plan *plan, spisor_refusal *refusal) {
    const catalog_round *round = catalog->round;
    if (event->timer == FOREBAY_CATALOG_PERIOD) {
        if (event->round != 0) {
            return forebay_refuse_member(refusal, "round", ROUND_RULE);
        }
        decide_request(catalog, event, plan);
        return SPISOR_OK;
    }
    if (!is_started(round, event->round)) {
        return forebay_refuse_member(refusal, "round", ROUND_RULE);
    }
    if (event->round == round->id && !round->closed) {
        plan->closes = 1;
        plan->derives = 1;
    }
    return SPISOR_OK;
}

spisor_status routing_catalog_decide(const forebay_catalog *catalog, const forebay_catalog_event_argument *event,
                                     catalog_plan *plan, spisor_refusal *refusal) {
    memset(plan, 0, sizeof *plan);
    switch (event->kind) {
    case FOREBAY_CATALOG_ANSWERED:
        return decide_answered(catalog->round, event, plan, refusal);
    case FOREBAY_CATALOG_TIMER:
        return decide_timer(catalog, event, plan, refusal);
    default:
        decide_request(catalog, event, plan);
        return SPISOR_OK;
    }
}
