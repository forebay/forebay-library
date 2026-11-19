#include <string.h>

#include "ir_json_reader.h"

static int is_object(ir_reader *reader, const cJSON *json, const char *what) {
    if (!cJSON_IsObject(json)) {
        ir_read_malformed(reader, what, "is not an object");
        return 0;
    }
    return 1;
}

static const char *const *read_texts(ir_reader *reader, const cJSON *array, size_t *count) {
    *count = 0;
    size_t size = array == NULL ? 0 : (size_t)cJSON_GetArraySize(array);
    const char **texts = ir_read_take(reader, size, sizeof *texts);
    if (texts == NULL) {
        return NULL;
    }
    size_t index = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, array) {
        if (!cJSON_IsString(item)) {
            ir_read_malformed(reader, "stopSequences", "holds an item that is not a string");
            return NULL;
        }
        texts[index++] = ir_read_copy(reader, item->valuestring);
    }
    *count = size;
    return texts;
}

static void read_message(ir_reader *reader, const cJSON *json, forebay_ir_message *message) {
    if (!is_object(reader, json, "messages")) {
        return;
    }
    message->role = ir_read_text(reader, json, "role", IR_REQUIRED);
    message->content = ir_read_blocks(reader, ir_read_array(reader, json, "content", IR_REQUIRED),
                                      &message->content_count);
    message->extensions = ir_read_record_opaque(reader, json, "extensions");
}

static const forebay_ir_message *read_messages(ir_reader *reader, const cJSON *array, size_t *count) {
    *count = 0;
    size_t size = array == NULL ? 0 : (size_t)cJSON_GetArraySize(array);
    forebay_ir_message *messages = ir_read_take(reader, size, sizeof *messages);
    if (messages == NULL) {
        return NULL;
    }
    size_t index = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, array) {
        read_message(reader, item, &messages[index++]);
    }
    *count = size;
    return messages;
}

static void read_tool(ir_reader *reader, const cJSON *json, forebay_ir_tool *tool) {
    if (!is_object(reader, json, "tools")) {
        return;
    }
    tool->name = ir_read_text(reader, json, "name", IR_REQUIRED);
    tool->description = ir_read_text(reader, json, "description", IR_OPTIONAL);
    tool->input_schema = ir_read_opaque(reader, json, "inputSchema", IR_REQUIRED);
    tool->extensions = ir_read_record_opaque(reader, json, "extensions");
}

static const forebay_ir_tool *read_tools(ir_reader *reader, const cJSON *array, size_t *count) {
    *count = 0;
    size_t size = array == NULL ? 0 : (size_t)cJSON_GetArraySize(array);
    forebay_ir_tool *tools = ir_read_take(reader, size, sizeof *tools);
    if (tools == NULL) {
        return NULL;
    }
    size_t index = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, array) {
        read_tool(reader, item, &tools[index++]);
    }
    *count = size;
    return tools;
}

static const forebay_ir_tool_choice *read_tool_choice(ir_reader *reader, const cJSON *object) {
    const cJSON *json = ir_read_member(reader, object, "toolChoice", IR_OPTIONAL);
    if (json == NULL || !is_object(reader, json, "toolChoice")) {
        return NULL;
    }
    forebay_ir_tool_choice *choice = ir_read_take(reader, 1, sizeof *choice);
    if (choice != NULL) {
        choice->type = ir_read_text(reader, json, "type", IR_REQUIRED);
        choice->name = ir_read_text(reader, json, "name", IR_OPTIONAL);
        choice->extensions = ir_read_record_opaque(reader, json, "extensions");
    }
    return choice;
}

static const forebay_ir_thinking *read_thinking(ir_reader *reader, const cJSON *object) {
    const cJSON *json = ir_read_member(reader, object, "thinking", IR_OPTIONAL);
    if (json == NULL || !is_object(reader, json, "thinking")) {
        return NULL;
    }
    forebay_ir_thinking *thinking = ir_read_take(reader, 1, sizeof *thinking);
    if (thinking != NULL) {
        uint32_t has_enabled = 0;
        ir_read_flag(reader, json, "enabled", IR_REQUIRED, &has_enabled, &thinking->enabled);
        ir_read_count(reader, json, "budgetTokens", IR_OPTIONAL, &thinking->has_budget_tokens,
                      &thinking->budget_tokens);
    }
    return thinking;
}

static void read_request_lists(ir_reader *reader, const cJSON *json, forebay_ir_request *request) {
    request->messages = read_messages(reader, ir_read_array(reader, json, "messages", IR_REQUIRED),
                                      &request->message_count);
    const cJSON *system = ir_read_array(reader, json, "system", IR_OPTIONAL);
    request->has_system = system != NULL;
    request->system = ir_read_blocks(reader, system, &request->system_count);
    const cJSON *tools = ir_read_array(reader, json, "tools", IR_OPTIONAL);
    request->has_tools = tools != NULL;
    request->tools = read_tools(reader, tools, &request->tool_count);
    const cJSON *stop_sequences = ir_read_array(reader, json, "stopSequences", IR_OPTIONAL);
    request->has_stop_sequences = stop_sequences != NULL;
    request->stop_sequences = read_texts(reader, stop_sequences, &request->stop_sequence_count);
}

static void read_request_numbers(ir_reader *reader, const cJSON *json, forebay_ir_request *request) {
    ir_read_count(reader, json, "maxTokens", IR_OPTIONAL, &request->has_max_tokens, &request->max_tokens);
    ir_read_number(reader, json, "temperature", &request->has_temperature, &request->temperature);
    ir_read_number(reader, json, "topP", &request->has_top_p, &request->top_p);
    ir_read_count(reader, json, "topK", IR_OPTIONAL, &request->has_top_k, &request->top_k);
}

spisor_status ir_json_read_request(const cJSON *json, ir_json_arena *arena, forebay_ir_request *request) {
    ir_reader reader = {arena, SPISOR_OK};
    *request = (forebay_ir_request){.size = (uint32_t)sizeof(forebay_ir_request)};
    if (!is_object(&reader, json, "request")) {
        return reader.status;
    }
    uint32_t has_stream = 0;
    request->model = ir_read_text(&reader, json, "model", IR_REQUIRED);
    ir_read_flag(&reader, json, "stream", IR_REQUIRED, &has_stream, &request->stream);
    read_request_lists(&reader, json, request);
    request->tool_choice = read_tool_choice(&reader, json);
    request->thinking = read_thinking(&reader, json);
    read_request_numbers(&reader, json, request);
    request->metadata = ir_read_record_opaque(&reader, json, "metadata");
    request->extensions = ir_read_record_opaque(&reader, json, "extensions");
    return reader.status;
}

spisor_status ir_json_read_response(const cJSON *json, ir_json_arena *arena, forebay_ir_response *response) {
    ir_reader reader = {arena, SPISOR_OK};
    *response = (forebay_ir_response){.size = (uint32_t)sizeof(forebay_ir_response)};
    if (!is_object(&reader, json, "response")) {
        return reader.status;
    }
    response->id = ir_read_text(&reader, json, "id", IR_REQUIRED);
    response->model = ir_read_text(&reader, json, "model", IR_REQUIRED);
    response->stop_reason = ir_read_text(&reader, json, "stopReason", IR_REQUIRED);
    response->content = ir_read_blocks(&reader, ir_read_array(&reader, json, "content", IR_REQUIRED),
                                       &response->content_count);
    response->usage = ir_read_usage(&reader, json, "usage");
    response->extensions = ir_read_record_opaque(&reader, json, "extensions");
    return reader.status;
}

spisor_status ir_json_read_block(const cJSON *json, ir_json_arena *arena, forebay_ir_block *block) {
    ir_reader reader = {arena, SPISOR_OK};
    *block = (forebay_ir_block){0};
    ir_read_block_into(&reader, json, block);
    return reader.status;
}

spisor_status ir_json_read_event(const cJSON *json, ir_json_arena *arena, forebay_ir_event *event) {
    ir_reader reader = {arena, SPISOR_OK};
    *event = (forebay_ir_event){0};
    ir_read_event_into(&reader, json, event);
    return reader.status;
}

spisor_status ir_json_read_events(const cJSON *json, ir_json_arena *arena, const forebay_ir_event **events,
                                  size_t *count) {
    ir_reader reader = {arena, SPISOR_OK};
    *events = NULL;
    *count = 0;
    if (!cJSON_IsArray(json)) {
        ir_read_malformed(&reader, "events", "is not an array");
        return reader.status;
    }
    size_t size = (size_t)cJSON_GetArraySize(json);
    forebay_ir_event *read = ir_read_take(&reader, size, sizeof *read);
    size_t index = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, json) {
        if (read != NULL) {
            ir_read_event_into(&reader, item, &read[index++]);
        }
    }
    if (reader.status == SPISOR_OK) {
        *events = read;
        *count = size;
    }
    return reader.status;
}

static int has_header_named(const forebay_pair *headers, size_t count, const char *name) {
    for (size_t index = 0; index < count; index++) {
        if (strcmp(headers[index].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static const forebay_pair *read_headers(ir_reader *reader, const cJSON *json, size_t *count) {
    *count = 0;
    if (json == NULL || !is_object(reader, json, "headers")) {
        return NULL;
    }
    size_t size = (size_t)cJSON_GetArraySize(json);
    forebay_pair *headers = ir_read_take(reader, size, sizeof *headers);
    if (headers == NULL) {
        return NULL;
    }
    size_t index = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, json) {
        if (!cJSON_IsString(item)) {
            ir_read_malformed(reader, item->string, "is a header that is not a string");
            return NULL;
        }
        if (has_header_named(headers, index, item->string)) {
            ir_read_malformed(reader, item->string, "is a header name given twice");
            return NULL;
        }
        headers[index].name = ir_read_copy(reader, item->string);
        headers[index].value = ir_read_copy(reader, item->valuestring);
        index++;
    }
    *count = size;
    return headers;
}

spisor_status ir_json_read_failure(const cJSON *json, ir_json_arena *arena, forebay_ir_failure *failure) {
    ir_reader reader = {arena, SPISOR_OK};
    *failure = (forebay_ir_failure){0};
    if (!is_object(&reader, json, "failure")) {
        return reader.status;
    }
    uint32_t has_status = 0;
    uint64_t status = 0;
    ir_read_count(&reader, json, "status", IR_REQUIRED, &has_status, &status);
    if (has_status && (status < 100 || status > 599)) {
        ir_read_malformed(&reader, "status", "is not from 100 to 599");
    }
    failure->status = (uint32_t)status;
    failure->headers = read_headers(&reader, ir_read_member(&reader, json, "headers", IR_REQUIRED),
                                    &failure->header_count);
    failure->body = ir_read_text(&reader, json, "body", IR_REQUIRED);
    ir_read_count(&reader, json, "retryAfterMilliseconds", IR_OPTIONAL, &failure->has_retry_after,
                  &failure->retry_after_milliseconds);
    return reader.status;
}
