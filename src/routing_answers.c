#include "routing_answers.h"

#include <string.h>

#include "answer_storage.h"
#include "definition_support.h"

spisor_status routing_served(service_reply *reply, spisor_refusal *refusal) {
    if (reply->status == SERVICE_OUT_OF_MEMORY) {
        return SPISOR_OUT_OF_MEMORY;
    }
    if (reply->status == SERVICE_MALFORMED) {
        return spisor_refuse_as(refusal, SPISOR_MALFORMED, reply->message, reply->fix);
    }
    const cJSON *refused = cJSON_GetObjectItemCaseSensitive(reply->result, "refused");
    if (!cJSON_IsString(refused)) {
        return SPISOR_OK;
    }
    spisor_status status = spisor_refuse_as(
        refusal, SPISOR_REFUSED, refused->valuestring,
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(reply->result, "fix")));
    cJSON_Delete(reply->result);
    reply->result = NULL;
    return status;
}

int routing_read_text(void **storage, const cJSON *object, const char *name, const char **text) {
    return forebay_storage_text(storage, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(object, name)), text);
}

int routing_read_texts(void **storage, const cJSON *json, const char *const **texts, size_t *count) {
    size_t total = (size_t)cJSON_GetArraySize(json);
    const char **read = forebay_storage_array(storage, total, sizeof *read);
    const cJSON *item = NULL;
    size_t index = 0;
    if (total > 0 && read == NULL) {
        return 0;
    }
    cJSON_ArrayForEach(item, json) {
        if (!forebay_storage_text(storage, cJSON_GetStringValue(item), &read[index++])) {
            return 0;
        }
    }
    *texts = read;
    *count = total;
    return 1;
}

int routing_read_pairs(void **storage, const cJSON *json, const forebay_pair **pairs, size_t *count) {
    size_t total = (size_t)cJSON_GetArraySize(json);
    forebay_pair *read = forebay_storage_array(storage, total, sizeof *read);
    const cJSON *member = NULL;
    size_t index = 0;
    if (total > 0 && read == NULL) {
        return 0;
    }
    cJSON_ArrayForEach(member, json) {
        forebay_pair *pair = &read[index++];
        if (!forebay_storage_text(storage, member->string, &pair->name) ||
            !forebay_storage_text(storage, cJSON_GetStringValue(member), &pair->value)) {
            return 0;
        }
    }
    *pairs = read;
    *count = total;
    return 1;
}

static int read_assignment(void **storage, const cJSON *json, forebay_assignment *assignment) {
    assignment->derived = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "derived")) ? 1u : 0u;
    return routing_read_text(storage, json, "provider", &assignment->provider) &&
           routing_read_text(storage, json, "model", &assignment->model) &&
           routing_read_text(storage, json, "name", &assignment->name);
}

int routing_read_assignments(void **storage, const cJSON *json, const forebay_assignment **chain, size_t *count) {
    size_t total = (size_t)cJSON_GetArraySize(json);
    forebay_assignment *read = forebay_storage_array(storage, total, sizeof *read);
    const cJSON *item = NULL;
    size_t index = 0;
    if (total > 0 && read == NULL) {
        return 0;
    }
    cJSON_ArrayForEach(item, json) {
        if (!read_assignment(storage, item, &read[index++])) {
            return 0;
        }
    }
    *chain = read;
    *count = total;
    return 1;
}

int routing_read_slots(void **storage, const cJSON *json, const forebay_slot **slots, size_t *count) {
    size_t total = (size_t)cJSON_GetArraySize(json);
    forebay_slot *read = forebay_storage_array(storage, total, sizeof *read);
    const cJSON *member = NULL;
    size_t index = 0;
    if (total > 0 && read == NULL) {
        return 0;
    }
    cJSON_ArrayForEach(member, json) {
        forebay_slot *slot = &read[index++];
        if (!forebay_storage_text(storage, member->string, &slot->slot) ||
            !routing_read_assignments(storage, member, &slot->chain, &slot->count)) {
            return 0;
        }
    }
    *slots = read;
    *count = total;
    return 1;
}

int routing_read_log(void **storage, const cJSON *json, const forebay_log_line **log, size_t *count) {
    size_t total = (size_t)cJSON_GetArraySize(json);
    forebay_log_line *read = forebay_storage_array(storage, total, sizeof *read);
    const cJSON *item = NULL;
    size_t index = 0;
    if (total > 0 && read == NULL) {
        return 0;
    }
    cJSON_ArrayForEach(item, json) {
        forebay_log_line *line = &read[index++];
        const char *level = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "level"));
        line->level = level != NULL && strcmp(level, "warn") == 0 ? FOREBAY_LOG_WARN : FOREBAY_LOG_INFO;
        if (!routing_read_text(storage, item, "message", &line->message)) {
            return 0;
        }
    }
    *log = read;
    *count = total;
    return 1;
}

uint32_t routing_reply_kind(const cJSON *reply) {
    static const char *const kinds[] = {"proxyError", "apiError", "providerFailure", "rateLimited"};
    const char *kind = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(reply, "kind"));
    for (uint32_t index = 0; kind != NULL && index < 4; index++) {
        if (strcmp(kinds[index], kind) == 0) {
            return FOREBAY_REPLY_PROXY_ERROR + index;
        }
    }
    return 0;
}

double routing_number(const cJSON *object, const char *name) {
    const cJSON *member = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsNumber(member) ? member->valuedouble : 0;
}
