#ifndef ROUTING_CATALOG_STATE_H
#define ROUTING_CATALOG_STATE_H

#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "forebay/routing.h"

/** One implementation's last answer, kept in a list the catalog owns, each node with its own storage chain. */
typedef struct catalog_kept {
    struct catalog_kept *next;
    const char *implementation;
    forebay_catalog_provider provider;
    void *storage;
} catalog_kept;

/** A round of asks: the implementations it asked, in order, which of them settled, and whether it closed. */
typedef struct catalog_round {
    uint64_t id;
    double started;
    int closed;
    const char **implementations;
    unsigned char *settled;
    size_t count;
    void *storage;
} catalog_round;

/**
 * @implNote Only the latest round is kept: an older one is either closed or superseded, and a superseded one's
 * requests wait for the latest's close, so the rounds waiting on it are always `first_waiting` up to the latest's id,
 * 0 for none.
 */
struct forebay_catalog {
    cJSON *profile;
    cJSON *settings;
    forebay_routing_profile defaults;
    catalog_kept *kept;
    catalog_round *round;
    uint64_t first_waiting;
    cJSON *map;
};

/** What an event does to the catalog, decided before anything is allocated. */
typedef struct catalog_plan {
    int starts;
    int supersedes;
    int waits;
    int stores;
    int settles;
    size_t settled;
    int closes;
    int clears_close_timer;
    int derives;
    int warns_failure;
    int sets_period;
} catalog_plan;

/** What an event needs allocated before it changes the catalog; the commit takes each piece it holds. */
typedef struct catalog_prepared {
    catalog_round *round;
    catalog_kept *kept;
    cJSON *map;
} catalog_prepared;

/**
 * Decides an event the definition checked: OK with `plan` filled, or MALFORMED, with the refusal filled, for a round
 * the catalog never started, a period timer naming a round, or an answer the latest round does not wait on.
 */
spisor_status routing_catalog_decide(const forebay_catalog *catalog, const forebay_catalog_event_argument *event,
                                     catalog_plan *plan, spisor_refusal *refusal);

/** The round the event starts or the latest one, which its plan acts on. */
const catalog_round *routing_catalog_acted_round(const forebay_catalog *catalog, const catalog_prepared *prepared);

/** A copy of `from` whose every text and array `*storage` owns; 0 when memory runs out. */
int routing_catalog_copy_provider(void **storage, const forebay_catalog_provider *from, forebay_catalog_provider *to);

/** A new round of `id` over the implementations; NULL when memory runs out. */
catalog_round *routing_catalog_new_round(uint64_t id, double started, const char *const *implementations,
                                         size_t count);

void routing_catalog_free_round(catalog_round *round);

/** A new kept answer of `implementation`; NULL when memory runs out. */
catalog_kept *routing_catalog_new_kept(const char *implementation, const forebay_catalog_provider *provider);

void routing_catalog_free_kept(catalog_kept *kept);

/** The kept answer of `implementation`, `fresh` before the catalog's own when it is that implementation's. */
const catalog_kept *routing_catalog_kept_of(const forebay_catalog *catalog, const catalog_kept *fresh,
                                            const char *implementation);

/** Appends a warning line of `prefix` then `text` to `log`; 0 when memory runs out. */
int routing_catalog_warn(cJSON *log, const char *prefix, const char *text);

/**
 * The model map over the answers `round` would read with `fresh` kept, into `*map`; a derivation `model_map` refuses
 * leaves `*map` NULL and appends its warning to `log`. OK, or OUT_OF_MEMORY with nothing taken.
 */
spisor_status routing_catalog_derive(const forebay_catalog *catalog, const catalog_round *round,
                                     const catalog_kept *fresh, cJSON **map, cJSON *log);

/** Takes what `prepared` holds into the catalog as `plan` says; it allocates nothing, so it cannot fail. */
void routing_catalog_commit(forebay_catalog *catalog, const catalog_plan *plan, catalog_prepared *prepared);

/** Fills the event's answer as `plan` says, the catalog not yet changed; 0 when memory runs out. */
int routing_catalog_fill_answer(const forebay_catalog *catalog, const catalog_plan *plan,
                                const forebay_catalog_event_argument *event, const catalog_prepared *prepared,
                                const cJSON *log, forebay_catalog_event_answer *answer);

/** Fills a read of the catalog as it stands; 0 when memory runs out. */
int routing_catalog_fill_read(const forebay_catalog *catalog, forebay_catalog_read_answer *answer);

#endif
