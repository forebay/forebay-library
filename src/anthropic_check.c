#include "anthropic_check.h"

#include "spisor/definition.h"

#define REQUEST_SHAPE SPISOR_SHAPE_THROUGH(forebay_ir_request, extensions)

static int refuse(anthropic_problem *problem, const char *member, const char *rule) {
    problem->member = member;
    problem->rule = rule;
    return 0;
}

static int is_array(const void *elements, size_t count) {
    return elements != NULL || count == 0;
}

static int check_block(const forebay_ir_block *block, anthropic_problem *problem);

static int check_blocks(const forebay_ir_block *blocks, size_t count, anthropic_problem *problem) {
    if (!is_array(blocks, count)) {
        return refuse(problem, "a block list", "present beside a nonzero count");
    }
    for (size_t index = 0; index < count; index++) {
        if (!check_block(&blocks[index], problem)) {
            return 0;
        }
    }
    return 1;
}

static int check_block_texts(const forebay_ir_block *block, anthropic_problem *problem) {
    switch (block->kind) {
    case FOREBAY_IR_BLOCK_TEXT:
        return block->text != NULL || refuse(problem, "a text block's text", "set");
    case FOREBAY_IR_BLOCK_IMAGE:
        return block->media_type != NULL || refuse(problem, "an image block's media_type", "set");
    case FOREBAY_IR_BLOCK_TOOL_USE:
        return (block->id != NULL && block->name != NULL) || refuse(problem, "a tool use's id and name", "set");
    case FOREBAY_IR_BLOCK_TOOL_RESULT:
        return block->tool_use_id != NULL || refuse(problem, "a tool result's tool_use_id", "set");
    case FOREBAY_IR_BLOCK_THINKING:
        return block->text != NULL || refuse(problem, "a thinking block's text", "set");
    default:
        return 1;
    }
}

static int check_block_values(const forebay_ir_block *block, anthropic_problem *problem) {
    if (block->kind == FOREBAY_IR_BLOCK_TOOL_USE && block->input == NULL) {
        return refuse(problem, "a tool use's input", "set, JSON null for none");
    }
    if (block->kind == FOREBAY_IR_BLOCK_UNKNOWN && (block->raw == NULL || block->raw->kind != SPISOR_VALUE_RECORD)) {
        return refuse(problem, "an unknown block's raw", "a record");
    }
    return 1;
}

static int check_block(const forebay_ir_block *block, anthropic_problem *problem) {
    if (block->kind < FOREBAY_IR_BLOCK_TEXT || block->kind > FOREBAY_IR_BLOCK_UNKNOWN) {
        return refuse(problem, "a block's kind", "a forebay_ir_block_kind");
    }
    if (!check_block_texts(block, problem) || !check_block_values(block, problem)) {
        return 0;
    }
    return block->kind != FOREBAY_IR_BLOCK_TOOL_RESULT || check_blocks(block->content, block->content_count, problem);
}

static int check_messages(const forebay_ir_request *request, anthropic_problem *problem) {
    if (!is_array(request->messages, request->message_count)) {
        return refuse(problem, "messages", "present beside a nonzero message_count");
    }
    for (size_t index = 0; index < request->message_count; index++) {
        const forebay_ir_message *message = &request->messages[index];
        if (message->role == NULL) {
            return refuse(problem, "a message's role", "set");
        }
        if (!check_blocks(message->content, message->content_count, problem)) {
            return 0;
        }
    }
    return 1;
}

static int check_tools(const forebay_ir_request *request, anthropic_problem *problem) {
    if (!is_array(request->tools, request->tool_count)) {
        return refuse(problem, "tools", "present beside a nonzero tool_count");
    }
    for (size_t index = 0; index < request->tool_count; index++) {
        if (request->tools[index].name == NULL || request->tools[index].input_schema == NULL) {
            return refuse(problem, "a tool's name and input_schema", "set");
        }
    }
    return 1;
}

static int check_stop_sequences(const forebay_ir_request *request, anthropic_problem *problem) {
    if (!is_array(request->stop_sequences, request->stop_sequence_count)) {
        return refuse(problem, "stop_sequences", "present beside a nonzero stop_sequence_count");
    }
    for (size_t index = 0; index < request->stop_sequence_count; index++) {
        if (request->stop_sequences[index] == NULL) {
            return refuse(problem, "a stop sequence", "set");
        }
    }
    return 1;
}

int anthropic_check_request(const forebay_ir_request *request, anthropic_problem *problem) {
    if (request == NULL || !spisor_has_shape(request->size, REQUEST_SHAPE)) {
        return refuse(problem, "request", "a forebay_ir_request that reaches its first shape");
    }
    if (request->model == NULL) {
        return refuse(problem, "model", "set");
    }
    if (request->tool_choice != NULL && request->tool_choice->type == NULL) {
        return refuse(problem, "tool_choice's type", "set");
    }
    return check_messages(request, problem) && check_blocks(request->system, request->system_count, problem) &&
           check_tools(request, problem) && check_stop_sequences(request, problem);
}
