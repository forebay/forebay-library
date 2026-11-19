#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ir_json_reader.h"

#define LARGEST_EXACT_INTEGER 9007199254740992.0

void ir_read_malformed(ir_reader *reader, const char *key, const char *why) {
    if (reader->status == SPISOR_OK) {
        fprintf(stderr, "IR member \"%s\": %s\n", key, why);
        reader->status = SPISOR_MALFORMED;
    }
}

void *ir_read_take(ir_reader *reader, size_t count, size_t size) {
    if (reader->status != SPISOR_OK || count == 0) {
        return NULL;
    }
    void *taken = size == 0 || count > SIZE_MAX / size ? NULL : ir_json_arena_take(reader->arena, count * size);
    if (taken == NULL) {
        reader->status = SPISOR_OUT_OF_MEMORY;
    }
    return taken;
}

const cJSON *ir_read_member(ir_reader *reader, const cJSON *object, const char *key, ir_presence presence) {
    if (reader->status != SPISOR_OK) {
        return NULL;
    }
    const cJSON *member = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsNull(member)) {
        member = NULL;
    }
    if (member == NULL && presence == IR_REQUIRED) {
        ir_read_malformed(reader, key, "is required");
    }
    return member;
}

const char *ir_read_text(ir_reader *reader, const cJSON *object, const char *key, ir_presence presence) {
    const cJSON *member = ir_read_member(reader, object, key, presence);
    if (member == NULL) {
        return NULL;
    }
    if (!cJSON_IsString(member)) {
        ir_read_malformed(reader, key, "is not a string");
        return NULL;
    }
    return ir_read_copy(reader, member->valuestring);
}

const char *ir_read_copy(ir_reader *reader, const char *text) {
    size_t length = strlen(text);
    char *copy = ir_read_take(reader, length + 1, 1);
    if (copy != NULL) {
        memcpy(copy, text, length + 1);
    }
    return copy;
}

void ir_read_count(ir_reader *reader, const cJSON *object, const char *key, ir_presence presence, uint32_t *has,
                   uint64_t *value) {
    const cJSON *member = ir_read_member(reader, object, key, presence);
    if (member == NULL) {
        return;
    }
    double number = member->valuedouble;
    if (!cJSON_IsNumber(member) || !(number >= 0 && number <= LARGEST_EXACT_INTEGER) || floor(number) != number) {
        ir_read_malformed(reader, key, "is not a count");
        return;
    }
    *has = 1;
    *value = (uint64_t)number;
}

void ir_read_number(ir_reader *reader, const cJSON *object, const char *key, uint32_t *has, double *value) {
    const cJSON *member = ir_read_member(reader, object, key, IR_OPTIONAL);
    if (member == NULL) {
        return;
    }
    if (!cJSON_IsNumber(member) || !isfinite(member->valuedouble)) {
        ir_read_malformed(reader, key, "is not a finite number");
        return;
    }
    *has = 1;
    *value = member->valuedouble;
}

void ir_read_flag(ir_reader *reader, const cJSON *object, const char *key, ir_presence presence, uint32_t *has,
                  uint32_t *value) {
    const cJSON *member = ir_read_member(reader, object, key, presence);
    if (member == NULL) {
        return;
    }
    if (!cJSON_IsBool(member)) {
        ir_read_malformed(reader, key, "is not a boolean");
        return;
    }
    *has = 1;
    *value = cJSON_IsTrue(member) ? 1 : 0;
}

const spisor_value *ir_read_opaque(ir_reader *reader, const cJSON *object, const char *key, ir_presence presence) {
    if (reader->status != SPISOR_OK) {
        return NULL;
    }
    const cJSON *member = cJSON_GetObjectItemCaseSensitive(object, key);
    if (member == NULL) {
        if (presence == IR_REQUIRED) {
            ir_read_malformed(reader, key, "is required");
        }
        return NULL;
    }
    spisor_status status = SPISOR_OK;
    const spisor_value *value = ir_json_arena_value(reader->arena, member, &status);
    if (status == SPISOR_MALFORMED) {
        ir_read_malformed(reader, key, "cannot be held as a value tree");
    } else if (status != SPISOR_OK) {
        reader->status = status;
    }
    return value;
}

const cJSON *ir_read_array(ir_reader *reader, const cJSON *object, const char *key, ir_presence presence) {
    const cJSON *member = ir_read_member(reader, object, key, presence);
    if (member != NULL && !cJSON_IsArray(member)) {
        ir_read_malformed(reader, key, "is not an array");
        return NULL;
    }
    return member;
}

const forebay_ir_usage *ir_read_usage(ir_reader *reader, const cJSON *object, const char *key) {
    const cJSON *json = ir_read_member(reader, object, key, IR_OPTIONAL);
    if (json == NULL) {
        return NULL;
    }
    if (!cJSON_IsObject(json)) {
        ir_read_malformed(reader, key, "is not an object");
        return NULL;
    }
    forebay_ir_usage *usage = ir_read_take(reader, 1, sizeof *usage);
    if (usage == NULL) {
        return NULL;
    }
    ir_read_count(reader, json, "cacheCreationInputTokens", IR_OPTIONAL, &usage->has_cache_creation_input_tokens,
                  &usage->cache_creation_input_tokens);
    ir_read_count(reader, json, "cacheReadInputTokens", IR_OPTIONAL, &usage->has_cache_read_input_tokens,
                  &usage->cache_read_input_tokens);
    ir_read_count(reader, json, "inputTokens", IR_OPTIONAL, &usage->has_input_tokens, &usage->input_tokens);
    ir_read_count(reader, json, "outputTokens", IR_OPTIONAL, &usage->has_output_tokens, &usage->output_tokens);
    ir_read_count(reader, json, "reasoningTokens", IR_OPTIONAL, &usage->has_reasoning_tokens,
                  &usage->reasoning_tokens);
    ir_read_count(reader, json, "totalTokens", IR_OPTIONAL, &usage->has_total_tokens, &usage->total_tokens);
    return usage;
}
