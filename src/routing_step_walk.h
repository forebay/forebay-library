#ifndef ROUTING_STEP_WALK_H
#define ROUTING_STEP_WALK_H

#include "routing_support.h"

/**
 * One request's way along its chain: the model asked for, the chain, the entry last called, the last
 * failure that let the walk go on, and, once an entry was rate limited, the last limited entry's
 * headers with the latest reset any of them named (0 for none), with the log lines this step adds.
 * The borrowed members point into the call being answered.
 */
typedef struct step_walk {
    const char *requested;
    const cJSON *chain;
    int index;
    char *failure;
    const cJSON *limited_headers;
    double limited_reset_at;
    cJSON *log;
} step_walk;

/** Appends a {level, message} line to the walk's log; answers 0 when memory runs out. */
int routing_walk_log(step_walk *walk, const char *level, const char *format, ...);

/** Takes message as the walk's failure; answers 0 when it is NULL. */
int routing_walk_remember_failure(step_walk *walk, char *message);

/** Keeps headers as the last limited entry's, and reset_at when it is later than any reset seen; 0 names none. */
void routing_walk_remember_limit(step_walk *walk, const cJSON *headers, double reset_at);

void routing_walk_discard(step_walk *walk);

const char *routing_walk_entry_member(const step_walk *walk, int index, const char *name);

/** Reads a walk handed back into walk; answers 0 with refusal filled, or with it clear when memory runs out. */
int routing_walk_read(const cJSON *json, step_walk *walk, routing_refusal *refusal);

#endif
