#include "ir_json_names.h"

#include <stddef.h>
#include <string.h>

#include "forebay/ir.h"

typedef struct kind_name {
    uint32_t kind;
    const char *name;
} kind_name;

static const kind_name BLOCK_KINDS[] = {
    {FOREBAY_IR_BLOCK_TEXT, "text"},           {FOREBAY_IR_BLOCK_IMAGE, "image"},
    {FOREBAY_IR_BLOCK_TOOL_USE, "tool_use"},   {FOREBAY_IR_BLOCK_TOOL_RESULT, "tool_result"},
    {FOREBAY_IR_BLOCK_THINKING, "thinking"},   {FOREBAY_IR_BLOCK_UNKNOWN, "unknown"},
};

static const kind_name EVENT_KINDS[] = {
    {FOREBAY_IR_EVENT_MESSAGE_START, "message_start"},
    {FOREBAY_IR_EVENT_CONTENT_BLOCK_START, "content_block_start"},
    {FOREBAY_IR_EVENT_TEXT_DELTA, "text_delta"},
    {FOREBAY_IR_EVENT_THINKING_DELTA, "thinking_delta"},
    {FOREBAY_IR_EVENT_THINKING_SIGNATURE, "thinking_signature"},
    {FOREBAY_IR_EVENT_TOOL_INPUT_DELTA, "tool_input_delta"},
    {FOREBAY_IR_EVENT_CONTENT_BLOCK_STOP, "content_block_stop"},
    {FOREBAY_IR_EVENT_MESSAGE_DELTA, "message_delta"},
    {FOREBAY_IR_EVENT_MESSAGE_STOP, "message_stop"},
    {FOREBAY_IR_EVENT_ERROR, "error"},
};

static const char *name_of(const kind_name *names, size_t count, uint32_t kind) {
    for (size_t index = 0; index < count; index++) {
        if (names[index].kind == kind) {
            return names[index].name;
        }
    }
    return NULL;
}

static uint32_t kind_of(const kind_name *names, size_t count, const char *name) {
    for (size_t index = 0; name != NULL && index < count; index++) {
        if (strcmp(names[index].name, name) == 0) {
            return names[index].kind;
        }
    }
    return 0;
}

const char *ir_json_block_kind_name(uint32_t kind) {
    return name_of(BLOCK_KINDS, sizeof BLOCK_KINDS / sizeof BLOCK_KINDS[0], kind);
}

uint32_t ir_json_block_kind_of(const char *name) {
    return kind_of(BLOCK_KINDS, sizeof BLOCK_KINDS / sizeof BLOCK_KINDS[0], name);
}

const char *ir_json_event_kind_name(uint32_t kind) {
    return name_of(EVENT_KINDS, sizeof EVENT_KINDS / sizeof EVENT_KINDS[0], kind);
}

uint32_t ir_json_event_kind_of(const char *name) {
    return kind_of(EVENT_KINDS, sizeof EVENT_KINDS / sizeof EVENT_KINDS[0], name);
}

int ir_json_is_started_block_kind(uint32_t kind) {
    return kind == FOREBAY_IR_BLOCK_TEXT || kind == FOREBAY_IR_BLOCK_TOOL_USE || kind == FOREBAY_IR_BLOCK_THINKING;
}
