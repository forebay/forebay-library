#include "ir_json_writer.h"
#include "spisor/json_value.h"

static void record_out_of_memory(ir_writer *writer) {
    if (writer->status == SPISOR_OK) {
        writer->status = SPISOR_OUT_OF_MEMORY;
    }
}

void ir_write_item(ir_writer *writer, cJSON *object, const char *key, cJSON *item) {
    if (item == NULL) {
        record_out_of_memory(writer);
        return;
    }
    if (writer->status != SPISOR_OK || object == NULL || !cJSON_AddItemToObject(object, key, item)) {
        cJSON_Delete(item);
        record_out_of_memory(writer);
    }
}

void ir_write_append(ir_writer *writer, cJSON *array, cJSON *item) {
    if (item == NULL) {
        record_out_of_memory(writer);
        return;
    }
    if (writer->status != SPISOR_OK || array == NULL || !cJSON_AddItemToArray(array, item)) {
        cJSON_Delete(item);
        record_out_of_memory(writer);
    }
}

cJSON *ir_write_array(ir_writer *writer) {
    if (writer->status != SPISOR_OK) {
        return NULL;
    }
    cJSON *array = cJSON_CreateArray();
    if (array == NULL) {
        record_out_of_memory(writer);
    }
    return array;
}

cJSON *ir_write_object(ir_writer *writer) {
    if (writer->status != SPISOR_OK) {
        return NULL;
    }
    cJSON *object = cJSON_CreateObject();
    if (object == NULL) {
        record_out_of_memory(writer);
    }
    return object;
}

void ir_write_text(ir_writer *writer, cJSON *object, const char *key, const char *text) {
    if (text != NULL && writer->status == SPISOR_OK) {
        ir_write_item(writer, object, key, cJSON_CreateString(text));
    }
}

void ir_write_count(ir_writer *writer, cJSON *object, const char *key, uint32_t has, uint64_t value) {
    if (has && writer->status == SPISOR_OK) {
        ir_write_item(writer, object, key, cJSON_CreateNumber((double)value));
    }
}

void ir_write_number(ir_writer *writer, cJSON *object, const char *key, uint32_t has, double value) {
    if (has && writer->status == SPISOR_OK) {
        ir_write_item(writer, object, key, cJSON_CreateNumber(value));
    }
}

void ir_write_flag(ir_writer *writer, cJSON *object, const char *key, uint32_t value) {
    if (writer->status == SPISOR_OK) {
        ir_write_item(writer, object, key, cJSON_CreateBool(value != 0));
    }
}

void ir_write_opaque(ir_writer *writer, cJSON *object, const char *key, const spisor_value *value) {
    if (value == NULL || writer->status != SPISOR_OK) {
        return;
    }
    cJSON *json = NULL;
    spisor_status status = spisor_json_of_value(value, &json);
    if (status != SPISOR_OK) {
        writer->status = status;
        return;
    }
    ir_write_item(writer, object, key, json);
}

void ir_write_usage(ir_writer *writer, cJSON *object, const char *key, const forebay_ir_usage *usage) {
    if (usage == NULL) {
        return;
    }
    cJSON *json = ir_write_object(writer);
    ir_write_count(writer, json, "cacheCreationInputTokens", usage->has_cache_creation_input_tokens,
                   usage->cache_creation_input_tokens);
    ir_write_count(writer, json, "cacheReadInputTokens", usage->has_cache_read_input_tokens,
                   usage->cache_read_input_tokens);
    ir_write_count(writer, json, "inputTokens", usage->has_input_tokens, usage->input_tokens);
    ir_write_count(writer, json, "outputTokens", usage->has_output_tokens, usage->output_tokens);
    ir_write_count(writer, json, "reasoningTokens", usage->has_reasoning_tokens, usage->reasoning_tokens);
    ir_write_count(writer, json, "totalTokens", usage->has_total_tokens, usage->total_tokens);
    ir_write_item(writer, object, key, json);
}

spisor_status ir_write_finish(ir_writer *writer, cJSON *built, cJSON **json) {
    if (writer->status == SPISOR_OK && built == NULL) {
        writer->status = SPISOR_OUT_OF_MEMORY;
    }
    if (writer->status != SPISOR_OK) {
        cJSON_Delete(built);
        built = NULL;
    }
    *json = built;
    return writer->status;
}
