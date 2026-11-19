#include "ir_json_names.h"
#include "ir_json_reader.h"

static void read_message_start(ir_reader *reader, const cJSON *json, forebay_ir_event *event) {
    event->id = ir_read_text(reader, json, "id", IR_OPTIONAL);
    event->model = ir_read_text(reader, json, "model", IR_OPTIONAL);
    event->role = ir_read_text(reader, json, "role", IR_OPTIONAL);
    event->usage = ir_read_usage(reader, json, "usage");
}

static void read_content_block_start(ir_reader *reader, const cJSON *json, forebay_ir_event *event) {
    const cJSON *block_kind = ir_read_member(reader, json, "blockKind", IR_REQUIRED);
    event->block_kind = ir_json_block_kind_of(cJSON_GetStringValue(block_kind));
    if (block_kind != NULL && !ir_json_is_started_block_kind(event->block_kind)) {
        ir_read_malformed(reader, "blockKind", "is not text, tool_use or thinking");
    }
    event->tool_use_id = ir_read_text(reader, json, "toolUseId", IR_OPTIONAL);
    event->tool_name = ir_read_text(reader, json, "toolName", IR_OPTIONAL);
}

static void read_message_delta(ir_reader *reader, const cJSON *json, forebay_ir_event *event) {
    event->stop_reason = ir_read_text(reader, json, "stopReason", IR_OPTIONAL);
    event->usage = ir_read_usage(reader, json, "usage");
}

static void read_error(ir_reader *reader, const cJSON *json, forebay_ir_event *event) {
    event->error_type = ir_read_text(reader, json, "errorType", IR_OPTIONAL);
    event->message = ir_read_text(reader, json, "message", IR_OPTIONAL);
}

static void read_members_of_kind(ir_reader *reader, const cJSON *json, forebay_ir_event *event) {
    switch (event->kind) {
    case FOREBAY_IR_EVENT_MESSAGE_START:
        read_message_start(reader, json, event);
        break;
    case FOREBAY_IR_EVENT_CONTENT_BLOCK_START:
        read_content_block_start(reader, json, event);
        break;
    case FOREBAY_IR_EVENT_TEXT_DELTA:
    case FOREBAY_IR_EVENT_THINKING_DELTA:
        event->text = ir_read_text(reader, json, "text", IR_OPTIONAL);
        break;
    case FOREBAY_IR_EVENT_THINKING_SIGNATURE:
        event->signature = ir_read_text(reader, json, "signature", IR_OPTIONAL);
        break;
    case FOREBAY_IR_EVENT_TOOL_INPUT_DELTA:
        event->partial_json = ir_read_text(reader, json, "partialJson", IR_OPTIONAL);
        break;
    case FOREBAY_IR_EVENT_MESSAGE_DELTA:
        read_message_delta(reader, json, event);
        break;
    case FOREBAY_IR_EVENT_ERROR:
        read_error(reader, json, event);
        break;
    default:
        break;
    }
}

void ir_read_event_into(ir_reader *reader, const cJSON *json, forebay_ir_event *event) {
    if (reader->status != SPISOR_OK) {
        return;
    }
    if (!cJSON_IsObject(json)) {
        ir_read_malformed(reader, "event", "is not an object");
        return;
    }
    const cJSON *kind = ir_read_member(reader, json, "event", IR_REQUIRED);
    event->kind = ir_json_event_kind_of(cJSON_GetStringValue(kind));
    if (kind != NULL && event->kind == 0) {
        ir_read_malformed(reader, "event", "names no event kind");
        return;
    }
    event->extensions = ir_read_record_opaque(reader, json, "extensions");
    if (ir_json_event_has_index(event->kind)) {
        uint32_t has_block_index = 0;
        ir_read_count(reader, json, "index", IR_REQUIRED, &has_block_index, &event->index);
    }
    read_members_of_kind(reader, json, event);
}
