#include "routing_step_answer.h"

#define MID_STREAM_ERROR_TYPE "api_error"

cJSON *routing_kind_step(const char *kind) {
    cJSON *step = cJSON_CreateObject();
    if (step == NULL || cJSON_AddStringToObject(step, "kind", kind) == NULL) {
        cJSON_Delete(step);
        return NULL;
    }
    return step;
}

static cJSON *step_carrying(const char *kind, const char *name, cJSON *item) {
    cJSON *step = routing_kind_step(kind);
    if (step == NULL) {
        cJSON_Delete(item);
        return NULL;
    }
    if (!service_add(step, name, item)) {
        cJSON_Delete(step);
        return NULL;
    }
    return step;
}

cJSON *routing_reply_step(const char *kind, int status, const char *message) {
    cJSON *reply = routing_kind_step(kind);
    if (reply == NULL || cJSON_AddNumberToObject(reply, "status", status) == NULL ||
        cJSON_AddStringToObject(reply, "message", message) == NULL) {
        cJSON_Delete(reply);
        return NULL;
    }
    return step_carrying("reply", "reply", reply);
}

cJSON *routing_end_stream_step(const char *message) {
    cJSON *event = cJSON_CreateObject();
    if (event == NULL || cJSON_AddStringToObject(event, "event", "error") == NULL ||
        cJSON_AddStringToObject(event, "errorType", MID_STREAM_ERROR_TYPE) == NULL ||
        cJSON_AddStringToObject(event, "message", message) == NULL) {
        cJSON_Delete(event);
        return NULL;
    }
    return step_carrying("endStream", "event", event);
}

cJSON *routing_call_provider_step(const step_walk *walk) {
    cJSON *step = routing_kind_step("callProvider");
    if (step == NULL ||
        cJSON_AddStringToObject(step, "provider", routing_walk_entry_member(walk, walk->index, "provider")) == NULL ||
        cJSON_AddStringToObject(step, "model", routing_walk_entry_member(walk, walk->index, "model")) == NULL) {
        cJSON_Delete(step);
        return NULL;
    }
    return step;
}

static cJSON *walk_json(const step_walk *walk) {
    cJSON *json = cJSON_CreateObject();
    if (json == NULL || cJSON_AddStringToObject(json, "requested", walk->requested) == NULL ||
        !service_add(json, "chain", cJSON_Duplicate(walk->chain, 1)) ||
        cJSON_AddNumberToObject(json, "index", walk->index) == NULL ||
        (walk->failure != NULL && cJSON_AddStringToObject(json, "failure", walk->failure) == NULL)) {
        cJSON_Delete(json);
        return NULL;
    }
    return json;
}

cJSON *routing_answer_of(step_walk *walk, cJSON *step, int expects_report) {
    cJSON *answer = service_wrap("step", step);
    cJSON *log = walk->log;
    walk->log = NULL;
    if (answer == NULL) {
        cJSON_Delete(log);
        return NULL;
    }
    if (!service_add(answer, "log", log) || (expects_report && !service_add(answer, "walk", walk_json(walk)))) {
        cJSON_Delete(answer);
        return NULL;
    }
    return answer;
}
