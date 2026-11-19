#include "anthropic_encode.h"

static int is_translator_key(const char *key) {
    return key[0] == '$';
}

void anthropic_set_extensions(anthropic_writer *writer, cJSON *object, const spisor_value *extensions,
                              int include_translator_keys) {
    if (anthropic_value_of_kind(extensions, SPISOR_VALUE_RECORD) == NULL) {
        return;
    }
    for (size_t index = 0; index < extensions->count; index++) {
        const spisor_entry *entry = &extensions->entries[index];
        if (include_translator_keys || !is_translator_key(entry->key)) {
            anthropic_set_value(writer, object, entry->key, &entry->value, "extensions");
        }
    }
}

static int is_absent(const spisor_value *value) {
    return value == NULL || value->kind == SPISOR_VALUE_NULL;
}

int anthropic_is_written_as_string(const spisor_value *extensions, const char *flag, const forebay_ir_block *blocks,
                                   size_t count) {
    return anthropic_extension_flag_is_set(extensions, flag) && count == 1 &&
           blocks[0].kind == FOREBAY_IR_BLOCK_TEXT && blocks[0].cache_control == NULL &&
           is_absent(blocks[0].extensions);
}

static cJSON *encode_image(anthropic_writer *writer, const forebay_ir_block *image) {
    cJSON *wire = anthropic_new_object(writer);
    const spisor_value *raw_source =
        anthropic_value_of_kind(anthropic_extension_of(image->extensions, ANTHROPIC_EXTENSION_IMAGE_SOURCE_RAW),
                                SPISOR_VALUE_RECORD);
    anthropic_set_text(writer, wire, "type", "image");
    if (raw_source != NULL) {
        anthropic_set_value(writer, wire, "source", raw_source, "an image's source");
    } else if (image->data != NULL || image->url != NULL) {
        cJSON *source = anthropic_new_object(writer);
        anthropic_set_text(writer, source, "type", image->data != NULL ? "base64" : "url");
        if (image->data != NULL) {
            anthropic_set_text(writer, source, "media_type", image->media_type);
            anthropic_set_text(writer, source, "data", image->data);
        } else {
            anthropic_set_text(writer, source, "url", image->url);
        }
        anthropic_set(writer, wire, "source", source);
    }
    return wire;
}

static cJSON *encode_tool_result(anthropic_writer *writer, const forebay_ir_block *result) {
    cJSON *wire = anthropic_new_object(writer);
    anthropic_set_text(writer, wire, "type", "tool_result");
    anthropic_set_text(writer, wire, "tool_use_id", result->tool_use_id);
    if (anthropic_is_written_as_string(result->extensions, ANTHROPIC_EXTENSION_CONTENT_IS_STRING, result->content,
                                       result->content_count)) {
        anthropic_set_text(writer, wire, "content", result->content[0].text);
    } else {
        anthropic_set(writer, wire, "content",
                      anthropic_encode_block_list(writer, result->content, result->content_count));
    }
    if (result->has_is_error) {
        anthropic_set_flag(writer, wire, "is_error", result->is_error);
    }
    return wire;
}

static cJSON *encode_thinking(anthropic_writer *writer, const forebay_ir_block *thinking) {
    cJSON *wire = anthropic_new_object(writer);
    if (anthropic_extension_flag_is_set(thinking->extensions, ANTHROPIC_EXTENSION_REDACTED)) {
        anthropic_set_text(writer, wire, "type", "redacted_thinking");
        const spisor_value *data = anthropic_extension_of(thinking->extensions, ANTHROPIC_EXTENSION_REDACTED_DATA);
        if (data != NULL) {
            anthropic_set_value(writer, wire, "data", data, "a redacted thinking block's data");
        }
        return wire;
    }
    anthropic_set_text(writer, wire, "type", "thinking");
    anthropic_set_text(writer, wire, "thinking", thinking->text);
    if (thinking->signature != NULL) {
        anthropic_set_text(writer, wire, "signature", thinking->signature);
    }
    return wire;
}

static cJSON *encode_block_body(anthropic_writer *writer, const forebay_ir_block *block) {
    cJSON *wire = NULL;
    switch (block->kind) {
    case FOREBAY_IR_BLOCK_IMAGE:
        return encode_image(writer, block);
    case FOREBAY_IR_BLOCK_TOOL_RESULT:
        return encode_tool_result(writer, block);
    case FOREBAY_IR_BLOCK_THINKING:
        return encode_thinking(writer, block);
    case FOREBAY_IR_BLOCK_TOOL_USE:
        wire = anthropic_new_object(writer);
        anthropic_set_text(writer, wire, "type", "tool_use");
        anthropic_set_text(writer, wire, "id", block->id);
        anthropic_set_text(writer, wire, "name", block->name);
        anthropic_set_value(writer, wire, "input", block->input, "a tool use's input");
        return wire;
    default:
        wire = anthropic_new_object(writer);
        anthropic_set_text(writer, wire, "type", "text");
        anthropic_set_text(writer, wire, "text", block->text);
        return wire;
    }
}

static void encode_cache_control(anthropic_writer *writer, const forebay_ir_block *block, cJSON *wire) {
    if (block->cache_control == NULL) {
        return;
    }
    cJSON *cache_control = anthropic_new_object(writer);
    anthropic_set_text(writer, cache_control, "type", block->cache_control);
    anthropic_set_record(writer, cache_control,
                         anthropic_extension_of(block->extensions, ANTHROPIC_EXTENSION_CACHE_CONTROL_EXTRA),
                         "a block's cache control extra");
    anthropic_set(writer, wire, "cache_control", cache_control);
}

static cJSON *encode_block(anthropic_writer *writer, const forebay_ir_block *block) {
    if (block->kind == FOREBAY_IR_BLOCK_UNKNOWN) {
        cJSON *wire = anthropic_new_object(writer);
        anthropic_set_record(writer, wire, block->raw, "an unknown block's raw");
        return wire;
    }
    cJSON *wire = encode_block_body(writer, block);
    encode_cache_control(writer, block, wire);
    anthropic_set_extensions(writer, wire, block->extensions, 0);
    return wire;
}

cJSON *anthropic_encode_block_list(anthropic_writer *writer, const forebay_ir_block *blocks, size_t count) {
    cJSON *list = anthropic_new_array(writer);
    for (size_t index = 0; index < count; index++) {
        anthropic_append(writer, list, encode_block(writer, &blocks[index]));
    }
    return list;
}
