#ifndef ROUTING_STEP_WALK_H
#define ROUTING_STEP_WALK_H

#include "routing_support.h"

/**
 * One request's way along its chain: the model asked for, the chain, the entry last called and the
 * last failure that let the walk go on, with the log lines this step adds.
 */
typedef struct step_walk {
    const char *requested;
    const cJSON *chain;
    int index;
    char *failure;
    cJSON *log;
} step_walk;

/** The printf formatting of format, as a new string, or NULL when memory runs out. */
char *routing_formatted(const char *format, ...);

/** Appends a {level, message} line to the walk's log; answers 0 when memory runs out. */
int routing_walk_log(step_walk *walk, const char *level, const char *format, ...);

/** Takes message as the walk's failure; answers 0 when it is NULL. */
int routing_walk_remember_failure(step_walk *walk, char *message);

void routing_walk_discard(step_walk *walk);

const char *routing_walk_entry_member(const step_walk *walk, int index, const char *name);

/** Reads a walk handed back into walk; answers 0 with refusal filled, or with it clear when memory runs out. */
int routing_walk_read(const cJSON *json, step_walk *walk, routing_refusal *refusal);

#endif
