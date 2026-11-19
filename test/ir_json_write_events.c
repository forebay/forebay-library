#include "ir_json_names.h"
#include "ir_json_writer.h"

static int has_index(uint32_t kind) {
    return kind >= FOREBAY_IR_EVENT_CONTENT_BLOCK_START && kind <= FOREBAY_IR_EVENT_CONTENT_BLOCK_STOP;
}

static void write_message_start(ir_writer *writer, cJSON *json, const forebay_ir_event *event) {
    ir_write_text(writer, json, "id", event->id);
    ir_write_text(writer, json, "model", event->model);
    ir_write_text(writer, json, "role", event->role);
    ir_write_usage(writer, json, "usage", event->usage);
}

static void write_content_block_start(ir_writer *writer, cJSON *json, const forebay_ir_event *event) {
    if (!ir_json_is_started_block_kind(event->block_kind) && writer->status == SPISOR_OK) {
        writer->status = SPISOR_MALFORMED;
    }
    ir_write_text(writer, json, "blockKind", ir_json_block_kind_name(event->block_kind));
    ir_write_text(writer, json, "toolUseId", event->tool_use_id);
    ir_write_text(writer, json, "toolName", event->tool_name);
}

static void write_members_of_kind(ir_writer *writer, cJSON *json, const forebay_ir_event *event) {
    switch (event->kind) {
    case FOREBAY_IR_EVENT_MESSAGE_START:
        write_message_start(writer, json, event);
        break;
    case FOREBAY_IR_EVENT_CONTENT_BLOCK_START:
        write_content_block_start(writer, json, event);
        break;
    case FOREBAY_IR_EVENT_TEXT_DELTA:
    case FOREBAY_IR_EVENT_THINKING_DELTA:
        ir_write_text(writer, json, "text", event->text);
        break;
    case FOREBAY_IR_EVENT_THINKING_SIGNATURE:
        ir_write_text(writer, json, "signature", event->signature);
        break;
    case FOREBAY_IR_EVENT_TOOL_INPUT_DELTA:
        ir_write_text(writer, json, "partialJson", event->partial_json);
        break;
    case FOREBAY_IR_EVENT_MESSAGE_DELTA:
        ir_write_text(writer, json, "stopReason", event->stop_reason);
        ir_write_usage(writer, json, "usage", event->usage);
        break;
    case FOREBAY_IR_EVENT_ERROR:
        ir_write_text(writer, json, "errorType", event->error_type);
        ir_write_text(writer, json, "message", event->message);
        break;
    default:
        break;
    }
}

cJSON *ir_write_event_object(ir_writer *writer, const forebay_ir_event *event) {
    const char *kind = ir_json_event_kind_name(event->kind);
    if (kind == NULL && writer->status == SPISOR_OK) {
        writer->status = SPISOR_MALFORMED;
    }
    cJSON *json = ir_write_object(writer);
    ir_write_text(writer, json, "event", kind);
    ir_write_opaque(writer, json, "extensions", event->extensions);
    ir_write_count(writer, json, "index", (uint32_t)has_index(event->kind), event->index);
    write_members_of_kind(writer, json, event);
    if (writer->status != SPISOR_OK) {
        cJSON_Delete(json);
        return NULL;
    }
    return json;
}
