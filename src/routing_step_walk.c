#include "routing_step_walk.h"

#include "spisor/allocation.h"

#include <stdarg.h>
#include <stdlib.h>

int routing_walk_log(step_walk *walk, const char *level, const char *format, ...) {
    va_list values;
    va_start(values, format);
    char *message = routing_formatted_list(format, values);
    va_end(values);
    cJSON *line = cJSON_CreateObject();
    int added = message != NULL && line != NULL && cJSON_AddStringToObject(line, "level", level) != NULL &&
                cJSON_AddStringToObject(line, "message", message) != NULL;
    spisor_memory_release(message);
    if (!added) {
        cJSON_Delete(line);
        return 0;
    }
    return service_push(walk->log, line);
}

int routing_walk_remember_failure(step_walk *walk, char *message) {
    spisor_memory_release(walk->failure);
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
    spisor_memory_release(walk->failure);
    cJSON_Delete(walk->log);
}

const char *routing_walk_entry_member(const step_walk *walk, int index, const char *name) {
    return cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(walk->chain, index), name)->valuestring;
}

static void read_limited(const cJSON *limited, step_walk *walk) {
    const cJSON *reset_at = cJSON_GetObjectItemCaseSensitive(limited, "resetAt");
    walk->limited_headers = cJSON_GetObjectItemCaseSensitive(limited, "headers");
    walk->limited_reset_at = reset_at == NULL ? 0 : reset_at->valuedouble;
}

int routing_walk_read(const cJSON *json, step_walk *walk) {
    const cJSON *failure = cJSON_GetObjectItemCaseSensitive(json, "failure");
    const cJSON *limited = cJSON_GetObjectItemCaseSensitive(json, "limited");
    if (limited != NULL) {
        read_limited(limited, walk);
    }
    walk->requested = cJSON_GetObjectItemCaseSensitive(json, "requested")->valuestring;
    walk->chain = cJSON_GetObjectItemCaseSensitive(json, "chain");
    walk->index = (int)cJSON_GetObjectItemCaseSensitive(json, "index")->valuedouble;
    return failure == NULL || routing_walk_remember_failure(walk, routing_formatted("%s", failure->valuestring));
}
