#include "anthropic_check.h"

#include "spisor/definition.h"

#define REQUEST_SHAPE SPISOR_SHAPE_THROUGH(forebay_ir_request, extensions)
#define RESPONSE_SHAPE SPISOR_SHAPE_THROUGH(forebay_ir_response, extensions)

static spisor_status refuse(anthropic_problem *problem, const char *member, const char *rule) {
    problem->member = member;
    problem->rule = rule;
    return SPISOR_MALFORMED;
}

/** Whether `condition` holds, else the refusal. */
static spisor_status require(int condition, anthropic_problem *problem, const char *member, const char *rule) {
    return condition ? SPISOR_OK : refuse(problem, member, rule);
}

/** A value tree, absent or well formed; the tree is walked before anything else reads it. */
static spisor_status check_value(const spisor_value *value, const char *member, anthropic_problem *problem) {
    spisor_status status = value == NULL ? SPISOR_OK : spisor_value_check(value);
    return status == SPISOR_MALFORMED ? refuse(problem, member, "a well-formed spisor_value") : status;
}

static int is_array(const void *elements, size_t count) {
    return elements != NULL || count == 0;
}

static spisor_status check_block(const forebay_ir_block *block, anthropic_problem *problem);

static spisor_status check_blocks(const forebay_ir_block *blocks, size_t count, anthropic_problem *problem) {
    if (!is_array(blocks, count)) {
        return refuse(problem, "a block list", "present beside a nonzero count");
    }
    for (size_t index = 0; index < count; index++) {
        spisor_status status = check_block(&blocks[index], problem);
        if (status != SPISOR_OK) {
            return status;
        }
    }
    return SPISOR_OK;
}

static spisor_status check_block_texts(const forebay_ir_block *block, anthropic_problem *problem) {
    switch (block->kind) {
    case FOREBAY_IR_BLOCK_TEXT:
        return require(block->text != NULL, problem, "a text block's text", "set");
    case FOREBAY_IR_BLOCK_IMAGE:
        return require(block->media_type != NULL, problem, "an image block's media_type", "set");
    case FOREBAY_IR_BLOCK_TOOL_USE:
        return require(block->id != NULL && block->name != NULL, problem, "a tool use's id and name", "set");
    case FOREBAY_IR_BLOCK_TOOL_RESULT:
        return require(block->tool_use_id != NULL, problem, "a tool result's tool_use_id", "set");
    case FOREBAY_IR_BLOCK_THINKING:
        return require(block->text != NULL, problem, "a thinking block's text", "set");
    default:
        return SPISOR_OK;
    }
}

static spisor_status check_block_values(const forebay_ir_block *block, anthropic_problem *problem) {
    if (block->kind == FOREBAY_IR_BLOCK_TOOL_USE && block->input == NULL) {
        return refuse(problem, "a tool use's input", "set, JSON null for none");
    }
    if (block->kind == FOREBAY_IR_BLOCK_UNKNOWN) {
        if (block->raw == NULL || block->raw->kind != SPISOR_VALUE_RECORD) {
            return refuse(problem, "an unknown block's raw", "a record");
        }
        return check_value(block->raw, "an unknown block's raw", problem);
    }
    return check_value(block->extensions, "a block's extensions", problem);
}

static spisor_status check_block(const forebay_ir_block *block, anthropic_problem *problem) {
    if (block->kind < FOREBAY_IR_BLOCK_TEXT || block->kind > FOREBAY_IR_BLOCK_UNKNOWN) {
        return refuse(problem, "a block's kind", "a forebay_ir_block_kind");
    }
    spisor_status status = check_block_texts(block, problem);
    if (status == SPISOR_OK) {
        status = check_block_values(block, problem);
    }
    if (status == SPISOR_OK && block->kind == FOREBAY_IR_BLOCK_TOOL_RESULT) {
        status = check_blocks(block->content, block->content_count, problem);
    }
    return status;
}

static spisor_status check_messages(const forebay_ir_request *request, anthropic_problem *problem) {
    if (!is_array(request->messages, request->message_count)) {
        return refuse(problem, "messages", "present beside a nonzero message_count");
    }
    for (size_t index = 0; index < request->message_count; index++) {
        const forebay_ir_message *message = &request->messages[index];
        spisor_status status = require(message->role != NULL, problem, "a message's role", "set");
        if (status == SPISOR_OK) {
            status = check_value(message->extensions, "a message's extensions", problem);
        }
        if (status == SPISOR_OK) {
            status = check_blocks(message->content, message->content_count, problem);
        }
        if (status != SPISOR_OK) {
            return status;
        }
    }
    return SPISOR_OK;
}

static spisor_status check_tools(const forebay_ir_request *request, anthropic_problem *problem) {
    if (!is_array(request->tools, request->tool_count)) {
        return refuse(problem, "tools", "present beside a nonzero tool_count");
    }
    for (size_t index = 0; index < request->tool_count; index++) {
        const forebay_ir_tool *tool = &request->tools[index];
        spisor_status status = require(tool->name != NULL && tool->input_schema != NULL, problem,
                                       "a tool's name and input_schema", "set");
        if (status == SPISOR_OK) {
            status = check_value(tool->extensions, "a tool's extensions", problem);
        }
        if (status != SPISOR_OK) {
            return status;
        }
    }
    return SPISOR_OK;
}

static spisor_status check_stop_sequences(const forebay_ir_request *request, anthropic_problem *problem) {
    if (!is_array(request->stop_sequences, request->stop_sequence_count)) {
        return refuse(problem, "stop_sequences", "present beside a nonzero stop_sequence_count");
    }
    for (size_t index = 0; index < request->stop_sequence_count; index++) {
        if (request->stop_sequences[index] == NULL) {
            return refuse(problem, "a stop sequence", "set");
        }
    }
    return SPISOR_OK;
}

static spisor_status check_tool_choice(const forebay_ir_request *request, anthropic_problem *problem) {
    if (request->tool_choice == NULL) {
        return SPISOR_OK;
    }
    spisor_status status = require(request->tool_choice->type != NULL, problem, "tool_choice's type", "set");
    return status == SPISOR_OK ? check_value(request->tool_choice->extensions, "tool_choice's extensions", problem)
                               : status;
}

spisor_status anthropic_check_request(const forebay_ir_request *request, anthropic_problem *problem) {
    if (request == NULL || !spisor_has_shape(request->size, REQUEST_SHAPE)) {
        return refuse(problem, "request", "a forebay_ir_request that reaches its first shape");
    }
    spisor_status status = require(request->model != NULL, problem, "model", "set");
    if (status == SPISOR_OK) {
        status = check_value(request->extensions, "the request's extensions", problem);
    }
    if (status == SPISOR_OK) {
        status = check_tool_choice(request, problem);
    }
    if (status == SPISOR_OK) {
        status = check_messages(request, problem);
    }
    if (status == SPISOR_OK) {
        status = check_blocks(request->system, request->system_count, problem);
    }
    if (status == SPISOR_OK) {
        status = check_tools(request, problem);
    }
    return status == SPISOR_OK ? check_stop_sequences(request, problem) : status;
}

spisor_status anthropic_check_response(const forebay_ir_response *response, anthropic_problem *problem) {
    if (response == NULL || !spisor_has_shape(response->size, RESPONSE_SHAPE)) {
        return refuse(problem, "response", "a forebay_ir_response that reaches its first shape");
    }
    spisor_status status = require(response->id != NULL && response->model != NULL && response->stop_reason != NULL,
                                   problem, "a response's id, model and stop_reason", "set");
    if (status == SPISOR_OK) {
        status = check_value(response->extensions, "the response's extensions", problem);
    }
    return status == SPISOR_OK ? check_blocks(response->content, response->content_count, problem) : status;
}
