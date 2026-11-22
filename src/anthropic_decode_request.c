#include <string.h>

#include "anthropic_decode.h"
#include "anthropic_wire.h"
#include "spisor/allocation.h"
#include "spisor/javascript.h"

/** $contentIsString. */
#define MESSAGE_TRANSLATOR_KEYS 1
/** $systemIsString and $thinkingType. */
#define REQUEST_TRANSLATOR_KEYS 2

static const char *const REQUEST_CONSUMED[] = {
    "model",   "system",     "messages", "tools", "tool_choice",    "max_tokens", "temperature",
    "top_p",   "top_k",      "stop_sequences",    "stream",         "thinking",   "metadata"};
static const char *const MESSAGE_CONSUMED[] = {"role", "content"};
static const char *const TOOL_CONSUMED[] = {"name", "description", "input_schema"};
static const char *const TOOL_CHOICE_CONSUMED[] = {"type", "name"};

static void decode_message(anthropic_reader *reader, const cJSON *wire, forebay_ir_message *message) {
    anthropic_extension_list extensions;
    anthropic_extensions_begin(reader, &extensions, MESSAGE_TRANSLATOR_KEYS, (size_t)cJSON_GetArraySize(wire));
    message->role = anthropic_copy_text(reader, anthropic_wire_string_or_empty(anthropic_wire_member(wire, "role")));
    anthropic_decode_content(reader, anthropic_wire_member(wire, "content"), ANTHROPIC_EXTENSION_CONTENT_IS_STRING,
                             &message->content, &message->content_count, &extensions);
    anthropic_extensions_put_unconsumed(reader, &extensions, wire, ANTHROPIC_KEYS_OF(MESSAGE_CONSUMED));
    message->extensions = anthropic_extensions_finish(reader, &extensions);
}

static const forebay_ir_message *decode_messages(anthropic_reader *reader, const cJSON *wire, size_t *count) {
    size_t size = anthropic_wire_map_count(anthropic_wire_list(wire));
    forebay_ir_message *messages = anthropic_take(reader, size, sizeof *messages);
    const cJSON *item = NULL;
    for (size_t index = 0; messages != NULL && index < size; index++) {
        item = anthropic_wire_next_map(wire, item);
        decode_message(reader, item, &messages[index]);
    }
    *count = messages == NULL ? 0 : size;
    return messages;
}

static void decode_tool(anthropic_reader *reader, const cJSON *wire, forebay_ir_tool *tool) {
    anthropic_extension_list extensions;
    anthropic_extensions_begin(reader, &extensions, 0, (size_t)cJSON_GetArraySize(wire));
    const cJSON *input_schema = anthropic_wire_member(wire, "input_schema");
    tool->name = anthropic_copy_text(reader, anthropic_wire_string_or_empty(anthropic_wire_member(wire, "name")));
    tool->description = anthropic_copy_text(reader, anthropic_wire_string(anthropic_wire_member(wire, "description")));
    tool->input_schema = input_schema == NULL || cJSON_IsNull(input_schema)
                             ? anthropic_null_value()
                             : anthropic_copy_value(reader, input_schema);
    anthropic_extensions_put_unconsumed(reader, &extensions, wire, ANTHROPIC_KEYS_OF(TOOL_CONSUMED));
    tool->extensions = anthropic_extensions_finish(reader, &extensions);
}

static const forebay_ir_tool *decode_tools(anthropic_reader *reader, const cJSON *wire, size_t *count) {
    size_t size = anthropic_wire_map_count(wire);
    forebay_ir_tool *tools = anthropic_take(reader, size, sizeof *tools);
    const cJSON *item = NULL;
    for (size_t index = 0; tools != NULL && index < size; index++) {
        item = anthropic_wire_next_map(wire, item);
        decode_tool(reader, item, &tools[index]);
    }
    *count = tools == NULL ? 0 : size;
    return tools;
}

static const forebay_ir_tool_choice *decode_tool_choice(anthropic_reader *reader, const cJSON *wire) {
    forebay_ir_tool_choice *choice = anthropic_take(reader, 1, sizeof *choice);
    if (choice == NULL) {
        return NULL;
    }
    anthropic_extension_list extensions;
    anthropic_extensions_begin(reader, &extensions, 0, (size_t)cJSON_GetArraySize(wire));
    choice->type = anthropic_copy_text(reader, anthropic_wire_string_or_empty(anthropic_wire_member(wire, "type")));
    choice->name = anthropic_copy_text(reader, anthropic_wire_string(anthropic_wire_member(wire, "name")));
    anthropic_extensions_put_unconsumed(reader, &extensions, wire, ANTHROPIC_KEYS_OF(TOOL_CHOICE_CONSUMED));
    choice->extensions = anthropic_extensions_finish(reader, &extensions);
    return choice;
}

/** The system prompt; a string is one text block flagged `$systemIsString`, and a value that is no list is absent. */
static void decode_system(anthropic_reader *reader, const cJSON *system, forebay_ir_request *request,
                          anthropic_extension_list *extensions) {
    request->has_system = (uint32_t)anthropic_decode_content(reader, system, ANTHROPIC_EXTENSION_SYSTEM_IS_STRING,
                                                             &request->system, &request->system_count, extensions);
}

static void decode_thinking(anthropic_reader *reader, const cJSON *wire, forebay_ir_request *request,
                            anthropic_extension_list *extensions) {
    const cJSON *map = anthropic_wire_map(wire);
    forebay_ir_thinking *thinking = map == NULL ? NULL : anthropic_take(reader, 1, sizeof *thinking);
    if (thinking == NULL) {
        return;
    }
    const cJSON *type = anthropic_wire_member(map, "type");
    const char *type_text = anthropic_wire_string(type);
    thinking->enabled = type_text == NULL || strcmp(type_text, "disabled") != 0 ? 1u : 0u;
    thinking->has_budget_tokens = (uint32_t)anthropic_wire_integer(anthropic_wire_member(map, "budget_tokens"),
                                                                   &thinking->budget_tokens);
    request->thinking = thinking;
    anthropic_extensions_put_json(reader, extensions, ANTHROPIC_EXTENSION_THINKING_TYPE,
                                  type_text == NULL ? NULL : type);
}

static const char *const *decode_stop_sequences(anthropic_reader *reader, const cJSON *list, size_t *count) {
    size_t size = (size_t)cJSON_GetArraySize(list);
    const char **sequences = anthropic_take(reader, size, sizeof *sequences);
    const cJSON *item = NULL;
    size_t index = 0;
    cJSON_ArrayForEach(item, list) {
        if (sequences == NULL) {
            break;
        }
        char *text = spisor_javascript_string(item);
        if (text == NULL) {
            reader->out_of_memory = 1;
            break;
        }
        sequences[index++] = anthropic_copy_text(reader, text);
        spisor_memory_release(text);
    }
    *count = sequences == NULL ? 0 : size;
    return sequences;
}

static void decode_scalars(anthropic_reader *reader, const cJSON *root, forebay_ir_request *request) {
    request->has_max_tokens = (uint32_t)anthropic_wire_integer(anthropic_wire_member(root, "max_tokens"),
                                                               &request->max_tokens);
    request->has_temperature = (uint32_t)anthropic_wire_number(anthropic_wire_member(root, "temperature"),
                                                               &request->temperature);
    request->has_top_p = (uint32_t)anthropic_wire_number(anthropic_wire_member(root, "top_p"), &request->top_p);
    request->has_top_k = (uint32_t)anthropic_wire_integer(anthropic_wire_member(root, "top_k"), &request->top_k);
    const cJSON *stop_sequences = anthropic_wire_list(anthropic_wire_member(root, "stop_sequences"));
    request->has_stop_sequences = stop_sequences != NULL;
    request->stop_sequences = decode_stop_sequences(reader, stop_sequences, &request->stop_sequence_count);
    const cJSON *metadata = anthropic_wire_map(anthropic_wire_member(root, "metadata"));
    request->metadata = metadata == NULL ? NULL : anthropic_copy_value(reader, metadata);
}

static void decode_members(anthropic_reader *reader, const cJSON *root, forebay_ir_request *request) {
    anthropic_extension_list extensions;
    anthropic_extensions_begin(reader, &extensions, REQUEST_TRANSLATOR_KEYS, (size_t)cJSON_GetArraySize(root));
    request->model = anthropic_copy_text(reader, anthropic_wire_string_or_empty(anthropic_wire_member(root, "model")));
    request->messages = decode_messages(reader, anthropic_wire_member(root, "messages"), &request->message_count);
    anthropic_wire_boolean(anthropic_wire_member(root, "stream"), &request->stream);
    decode_scalars(reader, root, request);
    decode_system(reader, anthropic_wire_member(root, "system"), request, &extensions);
    const cJSON *tools = anthropic_wire_list(anthropic_wire_member(root, "tools"));
    request->has_tools = tools != NULL;
    request->tools = decode_tools(reader, tools, &request->tool_count);
    const cJSON *tool_choice = anthropic_wire_map(anthropic_wire_member(root, "tool_choice"));
    request->tool_choice = tool_choice == NULL ? NULL : decode_tool_choice(reader, tool_choice);
    decode_thinking(reader, anthropic_wire_member(root, "thinking"), request, &extensions);
    anthropic_extensions_put_unconsumed(reader, &extensions, root, ANTHROPIC_KEYS_OF(REQUEST_CONSUMED));
    request->extensions = anthropic_extensions_finish(reader, &extensions);
}

const forebay_ir_request *anthropic_decode_request(anthropic_reader *reader, const cJSON *root) {
    forebay_ir_request *request = anthropic_take(reader, 1, sizeof *request);
    if (request == NULL) {
        return NULL;
    }
    request->size = (uint32_t)sizeof *request;
    decode_members(reader, root, request);
    return reader->out_of_memory ? NULL : request;
}
