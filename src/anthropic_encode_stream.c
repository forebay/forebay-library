#include "anthropic_encode.h"
#include "anthropic_stop_reason.h"

/** A new object whose `type` is `type`. */
static cJSON *typed_object(anthropic_writer *writer, const char *type) {
    cJSON *object = anthropic_new_object(writer);
    anthropic_set_text(writer, object, "type", type);
    return object;
}

/** Usage as the wire writes it, or the counts the translator writes for none: `default_input` adds input tokens. */
static cJSON *usage_or_default(anthropic_writer *writer, const forebay_ir_usage *usage, int default_input) {
    if (usage != NULL) {
        return anthropic_encode_usage(writer, usage);
    }
    cJSON *wire = anthropic_new_object(writer);
    if (default_input) {
        anthropic_set_count(writer, wire, "input_tokens", 0);
    }
    anthropic_set_count(writer, wire, "output_tokens", 0);
    return wire;
}

static cJSON *encode_message_start(anthropic_writer *writer, const forebay_ir_event *event) {
    cJSON *message = anthropic_new_object(writer);
    anthropic_set_text_or_null(writer, message, "id", event->id);
    anthropic_set_text(writer, message, "type", "message");
    anthropic_set_text_or_null(writer, message, "role", event->role);
    anthropic_set(writer, message, "content", anthropic_new_array(writer));
    anthropic_set_text_or_null(writer, message, "model", event->model);
    anthropic_set_text_or_null(writer, message, "stop_reason", NULL);
    anthropic_set_text_or_null(writer, message, "stop_sequence", NULL);
    anthropic_set(writer, message, "usage", usage_or_default(writer, event->usage, 1));
    anthropic_set_extensions(writer, message, event->extensions, 0);
    cJSON *data = typed_object(writer, "message_start");
    anthropic_set(writer, data, "message", message);
    return data;
}

static cJSON *content_block_of(anthropic_writer *writer, const forebay_ir_event *event) {
    if (event->block_kind == FOREBAY_IR_BLOCK_TOOL_USE) {
        cJSON *block = typed_object(writer, "tool_use");
        anthropic_set_text_or_null(writer, block, "id", event->tool_use_id);
        anthropic_set_text_or_null(writer, block, "name", event->tool_name);
        anthropic_set(writer, block, "input", anthropic_new_object(writer));
        return block;
    }
    int thinking = event->block_kind == FOREBAY_IR_BLOCK_THINKING;
    cJSON *block = typed_object(writer, thinking ? "thinking" : "text");
    anthropic_set_text(writer, block, thinking ? "thinking" : "text", "");
    return block;
}

/** A frame of `type` carrying the event's `index`. */
static cJSON *indexed_frame(anthropic_writer *writer, const char *type, const forebay_ir_event *event) {
    cJSON *data = typed_object(writer, type);
    anthropic_set_count(writer, data, "index", event->index);
    return data;
}

static cJSON *encode_content_block_start(anthropic_writer *writer, const forebay_ir_event *event) {
    cJSON *data = indexed_frame(writer, "content_block_start", event);
    anthropic_set(writer, data, "content_block", content_block_of(writer, event));
    return data;
}

/** A content block delta of `type` whose `member` holds `text`, null for none. */
static cJSON *encode_delta(anthropic_writer *writer, const forebay_ir_event *event, const char *type,
                           const char *member, const char *text) {
    cJSON *delta = typed_object(writer, type);
    anthropic_set_text_or_null(writer, delta, member, text);
    cJSON *data = indexed_frame(writer, "content_block_delta", event);
    anthropic_set(writer, data, "delta", delta);
    return data;
}

static cJSON *encode_message_delta(anthropic_writer *writer, const forebay_ir_event *event) {
    const spisor_value *stop_sequence =
        anthropic_extension_of(event->extensions, ANTHROPIC_EXTENSION_STOP_SEQUENCE_RAW);
    cJSON *delta = anthropic_new_object(writer);
    anthropic_set_text_or_null(writer, delta, "stop_reason",
                               event->stop_reason == NULL ? NULL : anthropic_stop_reason_to_wire(event->stop_reason));
    if (stop_sequence == NULL) {
        anthropic_set_text_or_null(writer, delta, "stop_sequence", NULL);
    } else {
        anthropic_set_value(writer, delta, "stop_sequence", stop_sequence, ANTHROPIC_EXTENSION_STOP_SEQUENCE_RAW);
    }
    anthropic_set_extensions(writer, delta, event->extensions, 0);
    cJSON *data = typed_object(writer, "message_delta");
    anthropic_set(writer, data, "delta", delta);
    anthropic_set(writer, data, "usage", usage_or_default(writer, event->usage, 0));
    return data;
}

static cJSON *encode_error(anthropic_writer *writer, const forebay_ir_event *event) {
    cJSON *error = anthropic_new_object(writer);
    anthropic_set_text_or_null(writer, error, "type", event->error_type);
    anthropic_set_text_or_null(writer, error, "message", event->message);
    cJSON *data = typed_object(writer, "error");
    anthropic_set(writer, data, "error", error);
    return data;
}

cJSON *anthropic_encode_stream_event(anthropic_writer *writer, const forebay_ir_event *event) {
    switch (event->kind) {
    case FOREBAY_IR_EVENT_MESSAGE_START:
        return encode_message_start(writer, event);
    case FOREBAY_IR_EVENT_CONTENT_BLOCK_START:
        return encode_content_block_start(writer, event);
    case FOREBAY_IR_EVENT_TEXT_DELTA:
        return encode_delta(writer, event, "text_delta", "text", event->text);
    case FOREBAY_IR_EVENT_THINKING_DELTA:
        return encode_delta(writer, event, "thinking_delta", "thinking", event->text);
    case FOREBAY_IR_EVENT_THINKING_SIGNATURE:
        return encode_delta(writer, event, "signature_delta", "signature", event->signature);
    case FOREBAY_IR_EVENT_TOOL_INPUT_DELTA:
        return encode_delta(writer, event, "input_json_delta", "partial_json", event->partial_json);
    case FOREBAY_IR_EVENT_CONTENT_BLOCK_STOP:
        return indexed_frame(writer, "content_block_stop", event);
    case FOREBAY_IR_EVENT_MESSAGE_DELTA:
        return encode_message_delta(writer, event);
    case FOREBAY_IR_EVENT_MESSAGE_STOP:
        return typed_object(writer, "message_stop");
    default:
        return encode_error(writer, event);
    }
}
