#include <string.h>

#include "anthropic_decode.h"
#include "anthropic_stop_reason.h"
#include "anthropic_wire.h"

/** $stopSequenceRaw. */
#define MESSAGE_DELTA_TRANSLATOR_KEYS 1

static const char *const MESSAGE_START_CONSUMED[] = {"id",          "type",          "role",  "content", "model",
                                                     "stop_reason", "stop_sequence", "usage"};
static const char *const MESSAGE_DELTA_CONSUMED[] = {"stop_reason", "stop_sequence"};

typedef int (*frame_decoder)(anthropic_reader *reader, const cJSON *frame, forebay_ir_event *event);

/** A frame's `index`, 0 when it is no count. */
static uint64_t index_of(const cJSON *frame) {
    uint64_t index = 0;
    return anthropic_wire_integer(anthropic_wire_member(frame, "index"), &index) ? index : 0;
}

/** A copy of the member when it is a string, else NULL. */
static const char *copy_string(anthropic_reader *reader, const cJSON *object, const char *key) {
    return anthropic_copy_text(reader, anthropic_wire_string(anthropic_wire_member(object, key)));
}

/** The members of `wire` nothing consumed, after what is already put, as the owner's extensions. */
static const spisor_value *finish_extensions(anthropic_reader *reader, anthropic_extension_list *extensions,
                                             const cJSON *wire, const char *const *consumed, size_t consumed_count) {
    anthropic_extensions_put_unconsumed(reader, extensions, wire, consumed, consumed_count);
    return anthropic_extensions_finish(reader, extensions);
}

static int decode_message_start(anthropic_reader *reader, const cJSON *frame, forebay_ir_event *event) {
    const cJSON *message = anthropic_wire_map(anthropic_wire_member(frame, "message"));
    if (message == NULL) {
        return 0;
    }
    anthropic_extension_list extensions;
    anthropic_extensions_begin(reader, &extensions, 0, (size_t)cJSON_GetArraySize(message));
    event->kind = FOREBAY_IR_EVENT_MESSAGE_START;
    event->id = copy_string(reader, message, "id");
    event->model = copy_string(reader, message, "model");
    event->role = copy_string(reader, message, "role");
    event->usage = anthropic_decode_usage(reader, anthropic_wire_member(message, "usage"));
    event->extensions = finish_extensions(reader, &extensions, message, ANTHROPIC_KEYS_OF(MESSAGE_START_CONSUMED));
    return 1;
}

static int decode_content_block_start(anthropic_reader *reader, const cJSON *frame, forebay_ir_event *event) {
    const cJSON *block = anthropic_wire_map(anthropic_wire_member(frame, "content_block"));
    const char *type = anthropic_wire_string(anthropic_wire_member(block, "type"));
    event->kind = FOREBAY_IR_EVENT_CONTENT_BLOCK_START;
    event->index = index_of(frame);
    if (type != NULL && strcmp(type, "tool_use") == 0) {
        event->block_kind = FOREBAY_IR_BLOCK_TOOL_USE;
        event->tool_use_id = copy_string(reader, block, "id");
        event->tool_name = copy_string(reader, block, "name");
    } else if (type != NULL && (strcmp(type, "thinking") == 0 || strcmp(type, "redacted_thinking") == 0)) {
        event->block_kind = FOREBAY_IR_BLOCK_THINKING;
    } else {
        event->block_kind = FOREBAY_IR_BLOCK_TEXT;
    }
    return 1;
}

/** A content block delta's type, the event kind it is, and the delta member that holds its text. */
typedef struct delta_kind {
    const char *type;
    uint32_t kind;
    const char *member;
} delta_kind;

static const delta_kind DELTA_KINDS[] = {
    {"text_delta", FOREBAY_IR_EVENT_TEXT_DELTA, "text"},
    {"input_json_delta", FOREBAY_IR_EVENT_TOOL_INPUT_DELTA, "partial_json"},
    {"thinking_delta", FOREBAY_IR_EVENT_THINKING_DELTA, "thinking"},
    {"signature_delta", FOREBAY_IR_EVENT_THINKING_SIGNATURE, "signature"},
};

/** Where an event of a delta kind keeps its text. */
static const char **delta_text_of(forebay_ir_event *event) {
    switch (event->kind) {
    case FOREBAY_IR_EVENT_TOOL_INPUT_DELTA:
        return &event->partial_json;
    case FOREBAY_IR_EVENT_THINKING_SIGNATURE:
        return &event->signature;
    default:
        return &event->text;
    }
}

static int decode_content_block_delta(anthropic_reader *reader, const cJSON *frame, forebay_ir_event *event) {
    const cJSON *delta = anthropic_wire_map(anthropic_wire_member(frame, "delta"));
    const char *type = anthropic_wire_string(anthropic_wire_member(delta, "type"));
    for (size_t index = 0; type != NULL && index < sizeof DELTA_KINDS / sizeof DELTA_KINDS[0]; index++) {
        if (strcmp(type, DELTA_KINDS[index].type) == 0) {
            event->kind = DELTA_KINDS[index].kind;
            event->index = index_of(frame);
            *delta_text_of(event) = copy_string(reader, delta, DELTA_KINDS[index].member);
            return 1;
        }
    }
    return 0;
}

static int decode_content_block_stop(anthropic_reader *reader, const cJSON *frame, forebay_ir_event *event) {
    (void)reader;
    event->kind = FOREBAY_IR_EVENT_CONTENT_BLOCK_STOP;
    event->index = index_of(frame);
    return 1;
}

static int decode_message_delta(anthropic_reader *reader, const cJSON *frame, forebay_ir_event *event) {
    const cJSON *delta = anthropic_wire_map(anthropic_wire_member(frame, "delta"));
    event->kind = FOREBAY_IR_EVENT_MESSAGE_DELTA;
    if (delta != NULL) {
        anthropic_extension_list extensions;
        anthropic_extensions_begin(reader, &extensions, MESSAGE_DELTA_TRANSLATOR_KEYS,
                                   (size_t)cJSON_GetArraySize(delta));
        event->stop_reason =
            anthropic_stop_reason_to_ir(anthropic_wire_string(anthropic_wire_member(delta, "stop_reason")));
        anthropic_extensions_put_json(reader, &extensions, ANTHROPIC_EXTENSION_STOP_SEQUENCE_RAW,
                                      anthropic_wire_member(delta, "stop_sequence"));
        event->extensions = finish_extensions(reader, &extensions, delta, ANTHROPIC_KEYS_OF(MESSAGE_DELTA_CONSUMED));
    }
    event->usage = anthropic_decode_usage(reader, anthropic_wire_member(frame, "usage"));
    return 1;
}

static int decode_message_stop(anthropic_reader *reader, const cJSON *frame, forebay_ir_event *event) {
    (void)reader;
    (void)frame;
    event->kind = FOREBAY_IR_EVENT_MESSAGE_STOP;
    return 1;
}

static int decode_error(anthropic_reader *reader, const cJSON *frame, forebay_ir_event *event) {
    const cJSON *error = anthropic_wire_map(anthropic_wire_member(frame, "error"));
    event->kind = FOREBAY_IR_EVENT_ERROR;
    event->error_type = copy_string(reader, error, "type");
    event->message = copy_string(reader, error, "message");
    return 1;
}

/** A frame type and how it is read. */
typedef struct frame_kind {
    const char *type;
    frame_decoder decode;
} frame_kind;

static const frame_kind FRAME_KINDS[] = {
    {"message_start", decode_message_start},
    {"content_block_start", decode_content_block_start},
    {"content_block_delta", decode_content_block_delta},
    {"content_block_stop", decode_content_block_stop},
    {"message_delta", decode_message_delta},
    {"message_stop", decode_message_stop},
    {"error", decode_error},
};

int anthropic_decode_stream_frame(anthropic_reader *reader, const cJSON *frame, forebay_ir_event *event) {
    const char *type = anthropic_wire_string(anthropic_wire_member(anthropic_wire_map(frame), "type"));
    for (size_t index = 0; type != NULL && index < sizeof FRAME_KINDS / sizeof FRAME_KINDS[0]; index++) {
        if (strcmp(type, FRAME_KINDS[index].type) == 0) {
            return FRAME_KINDS[index].decode(reader, frame, event);
        }
    }
    return 0;
}
