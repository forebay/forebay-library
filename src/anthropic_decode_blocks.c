#include <string.h>

#include "anthropic_decode.h"
#include "anthropic_wire.h"

/** $imageSourceRaw, $redacted, $redactedThinkingData, $contentIsString and $cacheControlExtra. */
#define BLOCK_TRANSLATOR_KEYS 5

static const char *const TEXT_CONSUMED[] = {"type", "cache_control", "text"};
static const char *const IMAGE_CONSUMED[] = {"type", "cache_control", "source"};
static const char *const TOOL_USE_CONSUMED[] = {"type", "cache_control", "id", "name", "input"};
static const char *const TOOL_RESULT_CONSUMED[] = {"type", "cache_control", "tool_use_id", "is_error", "content"};
static const char *const THINKING_CONSUMED[] = {"type", "cache_control", "thinking", "signature"};
static const char *const REDACTED_THINKING_CONSUMED[] = {"type", "cache_control", "data"};

static const spisor_value *value_or_null(anthropic_reader *reader, const cJSON *item) {
    return item == NULL || cJSON_IsNull(item) ? anthropic_null_value() : anthropic_copy_value(reader, item);
}

void anthropic_wrap_text(anthropic_reader *reader, const char *text, const forebay_ir_block **blocks, size_t *count) {
    forebay_ir_block *block = anthropic_take(reader, 1, sizeof *block);
    *blocks = block;
    *count = block == NULL ? 0 : 1;
    if (block != NULL) {
        block->kind = FOREBAY_IR_BLOCK_TEXT;
        block->text = anthropic_copy_text(reader, text);
    }
}

static void decode_text(anthropic_reader *reader, const cJSON *wire, forebay_ir_block *block,
                        anthropic_extension_list *extensions) {
    (void)extensions;
    block->kind = FOREBAY_IR_BLOCK_TEXT;
    block->text = anthropic_copy_text(reader, anthropic_wire_string_or_empty(anthropic_wire_member(wire, "text")));
}

static void decode_image_source(anthropic_reader *reader, const cJSON *source, forebay_ir_block *block,
                                anthropic_extension_list *extensions) {
    const char *source_type = anthropic_wire_string(anthropic_wire_member(source, "type"));
    if (source_type != NULL && strcmp(source_type, "base64") == 0) {
        const char *media_type = anthropic_wire_string_or_empty(anthropic_wire_member(source, "media_type"));
        block->media_type = anthropic_copy_text(reader, media_type);
        block->data = anthropic_copy_text(reader, anthropic_wire_string(anthropic_wire_member(source, "data")));
    } else if (source_type != NULL && strcmp(source_type, "url") == 0) {
        block->url = anthropic_copy_text(reader, anthropic_wire_string(anthropic_wire_member(source, "url")));
    } else {
        anthropic_extensions_put_json(reader, extensions, ANTHROPIC_EXTENSION_IMAGE_SOURCE_RAW, source);
    }
}

static void decode_image(anthropic_reader *reader, const cJSON *wire, forebay_ir_block *block,
                         anthropic_extension_list *extensions) {
    block->kind = FOREBAY_IR_BLOCK_IMAGE;
    block->media_type = "";
    const cJSON *source = anthropic_wire_map(anthropic_wire_member(wire, "source"));
    if (source != NULL) {
        decode_image_source(reader, source, block, extensions);
    }
}


static void decode_tool_result(anthropic_reader *reader, const cJSON *wire, forebay_ir_block *block,
                               anthropic_extension_list *extensions) {
    block->kind = FOREBAY_IR_BLOCK_TOOL_RESULT;
    const char *tool_use_id = anthropic_wire_string_or_empty(anthropic_wire_member(wire, "tool_use_id"));
    block->tool_use_id = anthropic_copy_text(reader, tool_use_id);
    block->has_is_error = (uint32_t)anthropic_wire_boolean(anthropic_wire_member(wire, "is_error"), &block->is_error);
    anthropic_decode_content(reader, anthropic_wire_member(wire, "content"), ANTHROPIC_EXTENSION_CONTENT_IS_STRING,
                             &block->content, &block->content_count, extensions);
}

static void decode_thinking(anthropic_reader *reader, const cJSON *wire, forebay_ir_block *block,
                            anthropic_extension_list *extensions) {
    (void)extensions;
    block->kind = FOREBAY_IR_BLOCK_THINKING;
    block->text = anthropic_copy_text(reader, anthropic_wire_string_or_empty(anthropic_wire_member(wire, "thinking")));
    block->signature = anthropic_copy_text(reader, anthropic_wire_string(anthropic_wire_member(wire, "signature")));
}

static void decode_redacted_thinking(anthropic_reader *reader, const cJSON *wire, forebay_ir_block *block,
                                     anthropic_extension_list *extensions) {
    block->kind = FOREBAY_IR_BLOCK_THINKING;
    block->text = "";
    anthropic_extensions_put_flag(reader, extensions, ANTHROPIC_EXTENSION_REDACTED);
    anthropic_extensions_put_value(reader, extensions, ANTHROPIC_EXTENSION_REDACTED_DATA,
                                   value_or_null(reader, anthropic_wire_member(wire, "data")));
}

static void decode_tool_use(anthropic_reader *reader, const cJSON *wire, forebay_ir_block *block,
                            anthropic_extension_list *extensions) {
    (void)extensions;
    block->kind = FOREBAY_IR_BLOCK_TOOL_USE;
    block->id = anthropic_copy_text(reader, anthropic_wire_string_or_empty(anthropic_wire_member(wire, "id")));
    block->name = anthropic_copy_text(reader, anthropic_wire_string_or_empty(anthropic_wire_member(wire, "name")));
    block->input = value_or_null(reader, anthropic_wire_member(wire, "input"));
}

typedef void (*block_decoder)(anthropic_reader *reader, const cJSON *wire, forebay_ir_block *block,
                              anthropic_extension_list *extensions);

typedef struct block_type {
    const char *type;
    block_decoder decode;
    const char *const *consumed;
    size_t consumed_count;
} block_type;

static const block_type BLOCK_TYPES[] = {
    {"text", decode_text, ANTHROPIC_KEYS_OF(TEXT_CONSUMED)},
    {"image", decode_image, ANTHROPIC_KEYS_OF(IMAGE_CONSUMED)},
    {"tool_use", decode_tool_use, ANTHROPIC_KEYS_OF(TOOL_USE_CONSUMED)},
    {"tool_result", decode_tool_result, ANTHROPIC_KEYS_OF(TOOL_RESULT_CONSUMED)},
    {"thinking", decode_thinking, ANTHROPIC_KEYS_OF(THINKING_CONSUMED)},
    {"redacted_thinking", decode_redacted_thinking, ANTHROPIC_KEYS_OF(REDACTED_THINKING_CONSUMED)},
};

static const block_type *block_type_of(const cJSON *wire) {
    const char *type = anthropic_wire_string(anthropic_wire_member(wire, "type"));
    for (size_t index = 0; type != NULL && index < sizeof BLOCK_TYPES / sizeof BLOCK_TYPES[0]; index++) {
        if (strcmp(BLOCK_TYPES[index].type, type) == 0) {
            return &BLOCK_TYPES[index];
        }
    }
    return NULL;
}

static int has_members_besides_type(const cJSON *record) {
    return cJSON_GetArraySize(record) > (cJSON_HasObjectItem(record, "type") ? 1 : 0);
}

/** The `type` of a cache control becomes the block's, and any other member of it is kept in an extension. */
static void decode_cache_control(anthropic_reader *reader, const cJSON *wire, forebay_ir_block *block,
                                 anthropic_extension_list *extensions) {
    const cJSON *cache_control = anthropic_wire_map(anthropic_wire_member(wire, "cache_control"));
    if (cache_control == NULL) {
        return;
    }
    block->cache_control =
        anthropic_copy_text(reader, anthropic_wire_string(anthropic_wire_member(cache_control, "type")));
    if (has_members_besides_type(cache_control)) {
        anthropic_extensions_put_value(reader, extensions, ANTHROPIC_EXTENSION_CACHE_CONTROL_EXTRA,
                                       anthropic_copy_record_without(reader, cache_control, "type"));
    }
}

static void decode_unknown(anthropic_reader *reader, const cJSON *wire, forebay_ir_block *block) {
    block->kind = FOREBAY_IR_BLOCK_UNKNOWN;
    block->raw = anthropic_copy_value(reader, wire);
}

static void decode_block(anthropic_reader *reader, const cJSON *wire, forebay_ir_block *block) {
    const block_type *type = block_type_of(wire);
    if (type == NULL) {
        decode_unknown(reader, wire, block);
        return;
    }
    anthropic_extension_list extensions;
    anthropic_extensions_begin(reader, &extensions, BLOCK_TRANSLATOR_KEYS, (size_t)cJSON_GetArraySize(wire));
    type->decode(reader, wire, block, &extensions);
    decode_cache_control(reader, wire, block, &extensions);
    anthropic_extensions_put_unconsumed(reader, &extensions, wire, type->consumed, type->consumed_count);
    block->extensions = anthropic_extensions_finish(reader, &extensions);
}

int anthropic_decode_content(anthropic_reader *reader, const cJSON *content, const char *string_flag,
                             const forebay_ir_block **blocks, size_t *count, anthropic_extension_list *extensions) {
    if (!cJSON_IsString(content)) {
        return anthropic_decode_blocks(reader, content, blocks, count);
    }
    anthropic_wrap_text(reader, content->valuestring, blocks, count);
    anthropic_extensions_put_flag(reader, extensions, string_flag);
    return 1;
}

int anthropic_decode_blocks(anthropic_reader *reader, const cJSON *wire, const forebay_ir_block **blocks,
                            size_t *count) {
    *blocks = NULL;
    *count = 0;
    if (anthropic_wire_list(wire) == NULL) {
        return 0;
    }
    size_t size = anthropic_wire_map_count(wire);
    forebay_ir_block *decoded = anthropic_take(reader, size, sizeof *decoded);
    const cJSON *item = NULL;
    for (size_t index = 0; decoded != NULL && index < size; index++) {
        item = anthropic_wire_next_map(wire, item);
        decode_block(reader, item, &decoded[index]);
    }
    *blocks = decoded;
    *count = decoded == NULL ? 0 : size;
    return 1;
}
