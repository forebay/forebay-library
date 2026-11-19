#include "ir_json_writer.h"

static cJSON *write_texts(ir_writer *writer, const char *const *texts, size_t count) {
    cJSON *array = ir_write_array(writer);
    for (size_t index = 0; index < count && writer->status == SPISOR_OK; index++) {
        ir_write_append(writer, array, cJSON_CreateString(texts[index]));
    }
    return array;
}

static cJSON *write_message(ir_writer *writer, const forebay_ir_message *message) {
    cJSON *json = ir_write_object(writer);
    ir_write_text(writer, json, "role", message->role);
    ir_write_item(writer, json, "content", ir_write_blocks(writer, message->content, message->content_count));
    ir_write_opaque(writer, json, "extensions", message->extensions);
    return json;
}

static cJSON *write_tool(ir_writer *writer, const forebay_ir_tool *tool) {
    cJSON *json = ir_write_object(writer);
    ir_write_text(writer, json, "name", tool->name);
    ir_write_text(writer, json, "description", tool->description);
    ir_write_opaque(writer, json, "inputSchema", tool->input_schema);
    ir_write_opaque(writer, json, "extensions", tool->extensions);
    return json;
}

static cJSON *write_messages(ir_writer *writer, const forebay_ir_message *messages, size_t count) {
    cJSON *array = ir_write_array(writer);
    for (size_t index = 0; index < count && writer->status == SPISOR_OK; index++) {
        ir_write_append(writer, array, write_message(writer, &messages[index]));
    }
    return array;
}

static cJSON *write_tools(ir_writer *writer, const forebay_ir_tool *tools, size_t count) {
    cJSON *array = ir_write_array(writer);
    for (size_t index = 0; index < count && writer->status == SPISOR_OK; index++) {
        ir_write_append(writer, array, write_tool(writer, &tools[index]));
    }
    return array;
}

static void write_tool_choice(ir_writer *writer, cJSON *object, const forebay_ir_tool_choice *choice) {
    if (choice == NULL) {
        return;
    }
    cJSON *json = ir_write_object(writer);
    ir_write_text(writer, json, "type", choice->type);
    ir_write_text(writer, json, "name", choice->name);
    ir_write_opaque(writer, json, "extensions", choice->extensions);
    ir_write_item(writer, object, "toolChoice", json);
}

static void write_thinking(ir_writer *writer, cJSON *object, const forebay_ir_thinking *thinking) {
    if (thinking == NULL) {
        return;
    }
    cJSON *json = ir_write_object(writer);
    ir_write_flag(writer, json, "enabled", thinking->enabled);
    ir_write_count(writer, json, "budgetTokens", thinking->has_budget_tokens, thinking->budget_tokens);
    ir_write_item(writer, object, "thinking", json);
}

static void write_request_lists(ir_writer *writer, cJSON *json, const forebay_ir_request *request) {
    ir_write_item(writer, json, "messages", write_messages(writer, request->messages, request->message_count));
    if (request->has_system) {
        ir_write_item(writer, json, "system", ir_write_blocks(writer, request->system, request->system_count));
    }
    if (request->has_tools) {
        ir_write_item(writer, json, "tools", write_tools(writer, request->tools, request->tool_count));
    }
    if (request->has_stop_sequences) {
        ir_write_item(writer, json, "stopSequences",
                      write_texts(writer, request->stop_sequences, request->stop_sequence_count));
    }
}

spisor_status ir_json_write_request(const forebay_ir_request *request, cJSON **json) {
    ir_writer writer = {SPISOR_OK};
    cJSON *built = ir_write_object(&writer);
    ir_write_text(&writer, built, "model", request->model);
    ir_write_flag(&writer, built, "stream", request->stream);
    write_request_lists(&writer, built, request);
    write_tool_choice(&writer, built, request->tool_choice);
    write_thinking(&writer, built, request->thinking);
    ir_write_count(&writer, built, "maxTokens", request->has_max_tokens, request->max_tokens);
    ir_write_number(&writer, built, "temperature", request->has_temperature, request->temperature);
    ir_write_number(&writer, built, "topP", request->has_top_p, request->top_p);
    ir_write_count(&writer, built, "topK", request->has_top_k, request->top_k);
    ir_write_opaque(&writer, built, "metadata", request->metadata);
    ir_write_opaque(&writer, built, "extensions", request->extensions);
    return ir_write_finish(&writer, built, json);
}

spisor_status ir_json_write_response(const forebay_ir_response *response, cJSON **json) {
    ir_writer writer = {SPISOR_OK};
    cJSON *built = ir_write_object(&writer);
    ir_write_text(&writer, built, "id", response->id);
    ir_write_text(&writer, built, "model", response->model);
    ir_write_text(&writer, built, "stopReason", response->stop_reason);
    ir_write_item(&writer, built, "content", ir_write_blocks(&writer, response->content, response->content_count));
    ir_write_usage(&writer, built, "usage", response->usage);
    ir_write_opaque(&writer, built, "extensions", response->extensions);
    return ir_write_finish(&writer, built, json);
}

spisor_status ir_json_write_block(const forebay_ir_block *block, cJSON **json) {
    ir_writer writer = {SPISOR_OK};
    return ir_write_finish(&writer, ir_write_block_object(&writer, block), json);
}

spisor_status ir_json_write_event(const forebay_ir_event *event, cJSON **json) {
    ir_writer writer = {SPISOR_OK};
    return ir_write_finish(&writer, ir_write_event_object(&writer, event), json);
}

spisor_status ir_json_write_events(const forebay_ir_event *events, size_t count, cJSON **json) {
    ir_writer writer = {SPISOR_OK};
    cJSON *array = ir_write_array(&writer);
    for (size_t index = 0; index < count && writer.status == SPISOR_OK; index++) {
        ir_write_append(&writer, array, ir_write_event_object(&writer, &events[index]));
    }
    return ir_write_finish(&writer, array, json);
}

static cJSON *write_headers(ir_writer *writer, const forebay_pair *headers, size_t count) {
    cJSON *json = ir_write_object(writer);
    for (size_t index = 0; index < count && writer->status == SPISOR_OK; index++) {
        ir_write_text(writer, json, headers[index].name, headers[index].value);
    }
    return json;
}

spisor_status ir_json_write_failure(const forebay_ir_failure *failure, cJSON **json) {
    ir_writer writer = {SPISOR_OK};
    cJSON *built = ir_write_object(&writer);
    ir_write_count(&writer, built, "status", 1, failure->status);
    ir_write_item(&writer, built, "headers", write_headers(&writer, failure->headers, failure->header_count));
    ir_write_text(&writer, built, "body", failure->body);
    ir_write_count(&writer, built, "retryAfterMilliseconds", failure->has_retry_after,
                   failure->retry_after_milliseconds);
    return ir_write_finish(&writer, built, json);
}
