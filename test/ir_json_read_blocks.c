#include "ir_json_names.h"
#include "ir_json_reader.h"

static void read_image(ir_reader *reader, const cJSON *json, forebay_ir_block *block) {
    block->media_type = ir_read_text(reader, json, "mediaType", IR_REQUIRED);
    block->data = ir_read_text(reader, json, "data", IR_OPTIONAL);
    block->url = ir_read_text(reader, json, "url", IR_OPTIONAL);
}

static void read_tool_use(ir_reader *reader, const cJSON *json, forebay_ir_block *block) {
    block->id = ir_read_text(reader, json, "id", IR_REQUIRED);
    block->name = ir_read_text(reader, json, "name", IR_REQUIRED);
    block->input = ir_read_opaque(reader, json, "input", IR_REQUIRED);
}

static void read_tool_result(ir_reader *reader, const cJSON *json, forebay_ir_block *block) {
    block->tool_use_id = ir_read_text(reader, json, "toolUseId", IR_REQUIRED);
    block->content = ir_read_blocks(reader, ir_read_array(reader, json, "content", IR_REQUIRED),
                                    &block->content_count);
    ir_read_flag(reader, json, "isError", IR_OPTIONAL, &block->has_is_error, &block->is_error);
}

static void read_thinking(ir_reader *reader, const cJSON *json, forebay_ir_block *block) {
    block->text = ir_read_text(reader, json, "text", IR_REQUIRED);
    block->signature = ir_read_text(reader, json, "signature", IR_OPTIONAL);
}

static void read_unknown(ir_reader *reader, const cJSON *json, forebay_ir_block *block) {
    block->raw = ir_read_opaque(reader, json, "raw", IR_REQUIRED);
    if (block->raw != NULL && block->raw->kind != SPISOR_VALUE_RECORD) {
        ir_read_malformed(reader, "raw", "is not an object");
    }
}

static void read_members_of_kind(ir_reader *reader, const cJSON *json, forebay_ir_block *block) {
    switch (block->kind) {
    case FOREBAY_IR_BLOCK_TEXT:
        block->text = ir_read_text(reader, json, "text", IR_REQUIRED);
        break;
    case FOREBAY_IR_BLOCK_IMAGE:
        read_image(reader, json, block);
        break;
    case FOREBAY_IR_BLOCK_TOOL_USE:
        read_tool_use(reader, json, block);
        break;
    case FOREBAY_IR_BLOCK_TOOL_RESULT:
        read_tool_result(reader, json, block);
        break;
    case FOREBAY_IR_BLOCK_THINKING:
        read_thinking(reader, json, block);
        break;
    case FOREBAY_IR_BLOCK_UNKNOWN:
        read_unknown(reader, json, block);
        break;
    default:
        break;
    }
}

void ir_read_block_into(ir_reader *reader, const cJSON *json, forebay_ir_block *block) {
    if (reader->status != SPISOR_OK) {
        return;
    }
    if (!cJSON_IsObject(json)) {
        ir_read_malformed(reader, "block", "is not an object");
        return;
    }
    const cJSON *kind = ir_read_member(reader, json, "kind", IR_REQUIRED);
    block->kind = ir_json_block_kind_of(cJSON_GetStringValue(kind));
    if (kind != NULL && block->kind == 0) {
        ir_read_malformed(reader, "kind", "names no block kind");
        return;
    }
    block->cache_control = ir_read_text(reader, json, "cacheControl", IR_OPTIONAL);
    block->extensions = ir_read_record_opaque(reader, json, "extensions");
    read_members_of_kind(reader, json, block);
}

const forebay_ir_block *ir_read_blocks(ir_reader *reader, const cJSON *array, size_t *count) {
    *count = 0;
    size_t size = array == NULL ? 0 : (size_t)cJSON_GetArraySize(array);
    forebay_ir_block *blocks = ir_read_take(reader, size, sizeof *blocks);
    if (blocks == NULL) {
        return NULL;
    }
    size_t index = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, array) {
        ir_read_block_into(reader, item, &blocks[index++]);
    }
    *count = size;
    return blocks;
}
