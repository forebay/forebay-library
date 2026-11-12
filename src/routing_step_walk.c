#include "routing_step_walk.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define WALK_FIX "hand back the walk the previous step answered, unchanged"

static char *formatted_list(const char *format, va_list values) {
    va_list copy;
    va_copy(copy, values);
    int length = vsnprintf(NULL, 0, format, values);
    char *text = length < 0 ? NULL : malloc((size_t)length + 1);
    if (text != NULL) {
        vsnprintf(text, (size_t)length + 1, format, copy);
    }
    va_end(copy);
    return text;
}

char *routing_formatted(const char *format, ...) {
    va_list values;
    va_start(values, format);
    char *text = formatted_list(format, values);
    va_end(values);
    return text;
}

int routing_walk_log(step_walk *walk, const char *level, const char *format, ...) {
    va_list values;
    va_start(values, format);
    char *message = formatted_list(format, values);
    va_end(values);
    cJSON *line = cJSON_CreateObject();
    int added = message != NULL && line != NULL && cJSON_AddStringToObject(line, "level", level) != NULL &&
                cJSON_AddStringToObject(line, "message", message) != NULL;
    free(message);
    if (!added) {
        cJSON_Delete(line);
        return 0;
    }
    return service_push(walk->log, line);
}

int routing_walk_remember_failure(step_walk *walk, char *message) {
    free(walk->failure);
    walk->failure = message;
    return message != NULL;
}

void routing_walk_remember_limit(step_walk *walk, const cJSON *headers, double reset_at) {
    walk->limited_headers = headers;
    if (reset_at > walk->limited_reset_at) {
        walk->limited_reset_at = reset_at;
    }
}

void routing_walk_discard(step_walk *walk) {
    free(walk->failure);
    cJSON_Delete(walk->log);
}

const char *routing_walk_entry_member(const step_walk *walk, int index, const char *name) {
    return cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(walk->chain, index), name)->valuestring;
}

static int refuse_walk(routing_refusal *refusal, const char *problem) {
    refusal->refused = 1;
    refusal->fix = WALK_FIX;
    snprintf(refusal->cause, sizeof(refusal->cause), "the walk is not one routing.step answered: %s", problem);
    return 0;
}

static int is_chain(const cJSON *chain) {
    if (!cJSON_IsArray(chain)) {
        return 0;
    }
    const cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, chain) {
        if (!cJSON_IsString(cJSON_GetObjectItemCaseSensitive(entry, "provider")) ||
            !cJSON_IsString(cJSON_GetObjectItemCaseSensitive(entry, "model"))) {
            return 0;
        }
    }
    return 1;
}

static int is_position_in(const cJSON *index, const cJSON *chain) {
    return cJSON_IsNumber(index) && index->valuedouble >= 0 && index->valuedouble < cJSON_GetArraySize(chain) &&
           (double)(int)index->valuedouble == index->valuedouble;
}

static int is_limited(const cJSON *limited) {
    const cJSON *reset_at = cJSON_GetObjectItemCaseSensitive(limited, "resetAt");
    return cJSON_IsObject(limited) && cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(limited, "headers")) &&
           (reset_at == NULL || (cJSON_IsNumber(reset_at) && reset_at->valuedouble > 0));
}

static void read_limited(const cJSON *limited, step_walk *walk) {
    const cJSON *reset_at = cJSON_GetObjectItemCaseSensitive(limited, "resetAt");
    walk->limited_headers = cJSON_GetObjectItemCaseSensitive(limited, "headers");
    walk->limited_reset_at = reset_at == NULL ? 0 : reset_at->valuedouble;
}

int routing_walk_read(const cJSON *json, step_walk *walk, routing_refusal *refusal) {
    const cJSON *requested = cJSON_GetObjectItemCaseSensitive(json, "requested");
    const cJSON *chain = cJSON_GetObjectItemCaseSensitive(json, "chain");
    const cJSON *index = cJSON_GetObjectItemCaseSensitive(json, "index");
    const cJSON *failure = cJSON_GetObjectItemCaseSensitive(json, "failure");
    const cJSON *limited = cJSON_GetObjectItemCaseSensitive(json, "limited");
    if (!cJSON_IsObject(json)) {
        return refuse_walk(refusal, "it is not an object");
    }
    if (!cJSON_IsString(requested)) {
        return refuse_walk(refusal, "its requested model is not a string");
    }
    if (!is_chain(chain)) {
        return refuse_walk(refusal, "its chain is not a list of entries naming a provider and a model");
    }
    if (!is_position_in(index, chain)) {
        return refuse_walk(refusal, "its index is not a position in its chain");
    }
    if (failure != NULL && !cJSON_IsString(failure)) {
        return refuse_walk(refusal, "its failure is not a string");
    }
    if (limited != NULL && !is_limited(limited)) {
        return refuse_walk(refusal, "its limited member is not headers with an optional positive resetAt");
    }
    if (limited != NULL) {
        read_limited(limited, walk);
    }
    walk->requested = requested->valuestring;
    walk->chain = chain;
    walk->index = (int)index->valuedouble;
    return failure == NULL || routing_walk_remember_failure(walk, routing_formatted("%s", failure->valuestring));
}
