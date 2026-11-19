#include <string.h>

#include "anthropic_encode.h"

static cJSON *encode_message(anthropic_writer *writer, const forebay_ir_message *message) {
    cJSON *wire = anthropic_new_object(writer);
    anthropic_set_text(writer, wire, "role", message->role);
    anthropic_set_content(writer, wire, "content", message->extensions, ANTHROPIC_EXTENSION_CONTENT_IS_STRING,
                          message->content, message->content_count);
    anthropic_set_extensions(writer, wire, message->extensions, 0);
    return wire;
}

static cJSON *encode_messages(anthropic_writer *writer, const forebay_ir_request *request) {
    cJSON *list = anthropic_new_array(writer);
    for (size_t index = 0; index < request->message_count; index++) {
        anthropic_append(writer, list, encode_message(writer, &request->messages[index]));
    }
    return list;
}

static cJSON *encode_tool(anthropic_writer *writer, const forebay_ir_tool *tool) {
    cJSON *wire = anthropic_new_object(writer);
    anthropic_set_text(writer, wire, "name", tool->name);
    if (tool->description != NULL) {
        anthropic_set_text(writer, wire, "description", tool->description);
    }
    anthropic_set_value(writer, wire, "input_schema", tool->input_schema, "a tool's input_schema");
    anthropic_set_extensions(writer, wire, tool->extensions, 1);
    return wire;
}

static cJSON *encode_tools(anthropic_writer *writer, const forebay_ir_request *request) {
    cJSON *list = anthropic_new_array(writer);
    for (size_t index = 0; index < request->tool_count; index++) {
        anthropic_append(writer, list, encode_tool(writer, &request->tools[index]));
    }
    return list;
}

static cJSON *encode_tool_choice(anthropic_writer *writer, const forebay_ir_tool_choice *choice) {
    cJSON *wire = anthropic_new_object(writer);
    anthropic_set_text(writer, wire, "type", choice->type);
    if (choice->name != NULL) {
        anthropic_set_text(writer, wire, "name", choice->name);
    }
    anthropic_set_extensions(writer, wire, choice->extensions, 1);
    return wire;
}

static void set_system(anthropic_writer *writer, cJSON *wire, const forebay_ir_request *request) {
    if (!request->has_system) {
        return;
    }
    anthropic_set_content(writer, wire, "system", request->extensions, ANTHROPIC_EXTENSION_SYSTEM_IS_STRING,
                          request->system, request->system_count);
}

static void set_scalars(anthropic_writer *writer, cJSON *wire, const forebay_ir_request *request) {
    if (request->has_max_tokens) {
        anthropic_set_count(writer, wire, "max_tokens", request->max_tokens);
    }
    if (request->has_temperature) {
        anthropic_set_number(writer, wire, "temperature", request->temperature);
    }
    if (request->has_top_p) {
        anthropic_set_number(writer, wire, "top_p", request->top_p);
    }
    if (request->has_top_k) {
        anthropic_set_count(writer, wire, "top_k", request->top_k);
    }
    if (request->has_stop_sequences) {
        cJSON *sequences = anthropic_new_array(writer);
        for (size_t index = 0; index < request->stop_sequence_count; index++) {
            anthropic_append(writer, sequences, cJSON_CreateString(request->stop_sequences[index]));
        }
        anthropic_set(writer, wire, "stop_sequences", sequences);
    }
}

/** The `type` recorded when the request was read, else what `enabled` says. */
static void set_thinking(anthropic_writer *writer, cJSON *wire, const forebay_ir_request *request) {
    if (request->thinking == NULL) {
        return;
    }
    const spisor_value *recorded = anthropic_value_of_kind(
        anthropic_extension_of(request->extensions, ANTHROPIC_EXTENSION_THINKING_TYPE), SPISOR_VALUE_TEXT);
    cJSON *thinking = anthropic_new_object(writer);
    anthropic_set_text(writer, thinking, "type",
                       recorded != NULL ? recorded->text : (request->thinking->enabled ? "enabled" : "disabled"));
    if (request->thinking->has_budget_tokens) {
        anthropic_set_count(writer, thinking, "budget_tokens", request->thinking->budget_tokens);
    }
    anthropic_set(writer, wire, "thinking", thinking);
}

cJSON *anthropic_encode_request_object(anthropic_writer *writer, const forebay_ir_request *request) {
    cJSON *wire = anthropic_new_object(writer);
    anthropic_set_text(writer, wire, "model", request->model);
    set_system(writer, wire, request);
    anthropic_set(writer, wire, "messages", encode_messages(writer, request));
    if (request->has_tools) {
        anthropic_set(writer, wire, "tools", encode_tools(writer, request));
    }
    if (request->tool_choice != NULL) {
        anthropic_set(writer, wire, "tool_choice", encode_tool_choice(writer, request->tool_choice));
    }
    set_scalars(writer, wire, request);
    anthropic_set_flag(writer, wire, "stream", request->stream);
    set_thinking(writer, wire, request);
    if (request->metadata != NULL && request->metadata->kind != SPISOR_VALUE_NULL) {
        anthropic_set_value(writer, wire, "metadata", request->metadata, "metadata");
    }
    anthropic_set_extensions(writer, wire, request->extensions, 0);
    return wire;
}
