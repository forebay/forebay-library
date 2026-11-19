#include "ir_json_names.h"
#include "ir_json_writer.h"

static void write_image(ir_writer *writer, cJSON *json, const forebay_ir_block *block) {
    ir_write_text(writer, json, "mediaType", block->media_type);
    ir_write_text(writer, json, "data", block->data);
    ir_write_text(writer, json, "url", block->url);
}

static void write_tool_use(ir_writer *writer, cJSON *json, const forebay_ir_block *block) {
    ir_write_text(writer, json, "id", block->id);
    ir_write_text(writer, json, "name", block->name);
    ir_write_opaque(writer, json, "input", block->input);
}

static void write_tool_result(ir_writer *writer, cJSON *json, const forebay_ir_block *block) {
    ir_write_text(writer, json, "toolUseId", block->tool_use_id);
    ir_write_item(writer, json, "content", ir_write_blocks(writer, block->content, block->content_count));
    if (block->has_is_error) {
        ir_write_flag(writer, json, "isError", block->is_error);
    }
}

static void write_thinking(ir_writer *writer, cJSON *json, const forebay_ir_block *block) {
    ir_write_text(writer, json, "text", block->text);
    ir_write_text(writer, json, "signature", block->signature);
}

static void write_members_of_kind(ir_writer *writer, cJSON *json, const forebay_ir_block *block) {
    switch (block->kind) {
    case FOREBAY_IR_BLOCK_TEXT:
        ir_write_text(writer, json, "text", block->text);
        break;
    case FOREBAY_IR_BLOCK_IMAGE:
        write_image(writer, json, block);
        break;
    case FOREBAY_IR_BLOCK_TOOL_USE:
        write_tool_use(writer, json, block);
        break;
    case FOREBAY_IR_BLOCK_TOOL_RESULT:
        write_tool_result(writer, json, block);
        break;
    case FOREBAY_IR_BLOCK_THINKING:
        write_thinking(writer, json, block);
        break;
    case FOREBAY_IR_BLOCK_UNKNOWN:
        ir_write_opaque(writer, json, "raw", block->raw);
        break;
    default:
        break;
    }
}

cJSON *ir_write_block_object(ir_writer *writer, const forebay_ir_block *block) {
    const char *kind = ir_json_block_kind_name(block->kind);
    if (kind == NULL && writer->status == SPISOR_OK) {
        writer->status = SPISOR_MALFORMED;
    }
    cJSON *json = ir_write_object(writer);
    ir_write_text(writer, json, "kind", kind);
    ir_write_text(writer, json, "cacheControl", block->cache_control);
    ir_write_opaque(writer, json, "extensions", block->extensions);
    write_members_of_kind(writer, json, block);
    if (writer->status != SPISOR_OK) {
        cJSON_Delete(json);
        return NULL;
    }
    return json;
}

cJSON *ir_write_blocks(ir_writer *writer, const forebay_ir_block *blocks, size_t count) {
    cJSON *array = ir_write_array(writer);
    for (size_t index = 0; index < count && writer->status == SPISOR_OK; index++) {
        ir_write_append(writer, array, ir_write_block_object(writer, &blocks[index]));
    }
    if (writer->status != SPISOR_OK) {
        cJSON_Delete(array);
        return NULL;
    }
    return array;
}
