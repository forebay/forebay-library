#ifndef FOREBAY_PUBLIC_IR_H
#define FOREBAY_PUBLIC_IR_H

/**
 * @file
 * The intermediate representation every translator and provider speaks: a request, a response, their blocks and the
 * events of a stream, member for member as `contracts/ir.schema.json` defines them. An IR value an operation answers
 * is the provider's, held by that operation's answer until the release for its kind; an IR value the caller builds
 * and hands in is borrowed for the call. A size-prefixed struct is read only up to its `size`; every other struct
 * here is an element or a member of one, carries no size, and changes only with the declaration's version. Text is
 * NUL-terminated UTF-8; text the caller builds that is not valid UTF-8 is written as it is, so the body it reaches is
 * not valid JSON. An array is `count` elements at its pointer, which may be NULL only when `count` is 0.
 *
 * The schema's shared definitions map the same way everywhere. A nullable text (NullableString) is NULL when it is
 * null or absent. A nullable count (NullableCount) and any other nullable number has a `has_` flag beside it, 1 when
 * it is present, since 0 is a real value. A count is written as a double, so one above 2^53 is not exact, and a
 * binding holds it in a double, as TypeScript's `number` does. A block index (BlockIndex) is a plain count. An
 * `extensions` record, an unknown block's `raw`, a tool use's `input`, a tool's `input_schema` and a request's
 * `metadata` are spisor_value trees, since their shape belongs to the client or the vendor: NULL when absent, and a
 * node of kind NULL when the member is present and null. A stop reason (IrStopReason) is text, never a closed enum,
 * so a reason a vendor adds later survives decoding and encoding unchanged.
 */

#include <stddef.h>
#include <stdint.h>

#include "forebay/pair.h"
#include "spisor/value.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Token counts, each present when its `has_` flag is 1. */
typedef struct forebay_ir_usage {
    uint32_t has_cache_creation_input_tokens;
    uint64_t cache_creation_input_tokens;
    uint32_t has_cache_read_input_tokens;
    uint64_t cache_read_input_tokens;
    uint32_t has_input_tokens;
    uint64_t input_tokens;
    uint32_t has_output_tokens;
    uint64_t output_tokens;
    uint32_t has_reasoning_tokens;
    uint64_t reasoning_tokens;
    uint32_t has_total_tokens;
    uint64_t total_tokens;
} forebay_ir_usage;

/** A tool the model may call: `name` and `input_schema` required, `description` NULL for none. */
typedef struct forebay_ir_tool {
    const char *name;
    const char *description;
    const spisor_value *input_schema;
    const spisor_value *extensions;
} forebay_ir_tool;

/** How the model may choose a tool: `type` required, as the front door spells it, and `name` NULL for none. */
typedef struct forebay_ir_tool_choice {
    const char *type;
    const char *name;
    const spisor_value *extensions;
} forebay_ir_tool_choice;

/** Whether the model thinks, and its budget when `has_budget_tokens` is 1. */
typedef struct forebay_ir_thinking {
    uint32_t enabled;
    uint32_t has_budget_tokens;
    uint64_t budget_tokens;
} forebay_ir_thinking;

/** What a block is. Append-only: a published value keeps its number. */
typedef enum forebay_ir_block_kind {
    FOREBAY_IR_BLOCK_TEXT = 1,
    FOREBAY_IR_BLOCK_IMAGE = 2,
    FOREBAY_IR_BLOCK_TOOL_USE = 3,
    FOREBAY_IR_BLOCK_TOOL_RESULT = 4,
    FOREBAY_IR_BLOCK_THINKING = 5,
    FOREBAY_IR_BLOCK_UNKNOWN = 6
} forebay_ir_block_kind;

/**
 * One block of content, carrying the members of every kind; those of other kinds are NULL or zero. Every kind has
 * `cache_control` and `extensions`. Text has `text`, required. Image has `media_type`, required, and `data` or `url`.
 * Tool use has `id`, `name` and `input`, all required. Tool result has `tool_use_id`, required, its `content` blocks,
 * and `is_error` when `has_is_error` is 1. Thinking has `text`, required, and `signature`. Unknown has `raw`, a record
 * and required: the vendor's block as it arrived.
 */
typedef struct forebay_ir_block {
    /** A forebay_ir_block_kind. */
    uint32_t kind;
    const char *cache_control;
    const spisor_value *extensions;
    const char *text;
    const char *media_type;
    const char *data;
    const char *url;
    const char *id;
    const char *name;
    const spisor_value *input;
    const char *tool_use_id;
    const struct forebay_ir_block *content;
    size_t content_count;
    uint32_t has_is_error;
    uint32_t is_error;
    const char *signature;
    const spisor_value *raw;
} forebay_ir_block;

/** One turn of the conversation: `role` required, and its blocks. */
typedef struct forebay_ir_message {
    const char *role;
    const forebay_ir_block *content;
    size_t content_count;
    const spisor_value *extensions;
} forebay_ir_message;

/**
 * A request: `model` required, its messages, and `stream` 1 to stream the response. `system`, `tools` and
 * `stop_sequences` are present when their `has_` flag is 1, so an empty list stays apart from none; `tool_choice` and
 * `thinking` are NULL for none, and each number is present when its `has_` flag is 1. First shape ends at
 * `extensions`.
 */
typedef struct forebay_ir_request {
    uint32_t size;
    const char *model;
    const forebay_ir_message *messages;
    size_t message_count;
    uint32_t stream;
    uint32_t has_system;
    const forebay_ir_block *system;
    size_t system_count;
    uint32_t has_tools;
    const forebay_ir_tool *tools;
    size_t tool_count;
    const forebay_ir_tool_choice *tool_choice;
    const forebay_ir_thinking *thinking;
    uint32_t has_max_tokens;
    uint64_t max_tokens;
    uint32_t has_temperature;
    double temperature;
    uint32_t has_top_p;
    double top_p;
    uint32_t has_top_k;
    uint64_t top_k;
    uint32_t has_stop_sequences;
    const char *const *stop_sequences;
    size_t stop_sequence_count;
    const spisor_value *metadata;
    const spisor_value *extensions;
} forebay_ir_request;

/**
 * A whole response: `id`, `model` and `stop_reason` required, the stop reason as text, its blocks, and `usage` NULL
 * for none. First shape ends at `extensions`.
 */
typedef struct forebay_ir_response {
    uint32_t size;
    const char *id;
    const char *model;
    const char *stop_reason;
    const forebay_ir_block *content;
    size_t content_count;
    const forebay_ir_usage *usage;
    const spisor_value *extensions;
} forebay_ir_response;

/** What a stream event is. Append-only: a published value keeps its number. */
typedef enum forebay_ir_event_kind {
    FOREBAY_IR_EVENT_MESSAGE_START = 1,
    FOREBAY_IR_EVENT_CONTENT_BLOCK_START = 2,
    FOREBAY_IR_EVENT_TEXT_DELTA = 3,
    FOREBAY_IR_EVENT_THINKING_DELTA = 4,
    FOREBAY_IR_EVENT_THINKING_SIGNATURE = 5,
    FOREBAY_IR_EVENT_TOOL_INPUT_DELTA = 6,
    FOREBAY_IR_EVENT_CONTENT_BLOCK_STOP = 7,
    FOREBAY_IR_EVENT_MESSAGE_DELTA = 8,
    FOREBAY_IR_EVENT_MESSAGE_STOP = 9,
    FOREBAY_IR_EVENT_ERROR = 10
} forebay_ir_event_kind;

/**
 * One stream event, carrying the members of every kind; those of other kinds are NULL or zero. Every kind has
 * `extensions`. Message start has `id`, `model`, `role` and `usage`. Content block start, the deltas, the signature and
 * content block stop have `index`, required. Content block start has `block_kind`, required and one of text, tool use
 * or thinking, and `tool_use_id` and `tool_name`. Text and thinking deltas have `text`, the signature `signature`, and
 * the tool input delta `partial_json`. Message delta has `stop_reason`, text, and `usage`. Error has `error_type` and
 * `message`. Every text and `usage` is NULL for none.
 */
typedef struct forebay_ir_event {
    /** A forebay_ir_event_kind. */
    uint32_t kind;
    const spisor_value *extensions;
    const char *id;
    const char *model;
    const char *role;
    const forebay_ir_usage *usage;
    uint64_t index;
    /** A forebay_ir_block_kind. */
    uint32_t block_kind;
    const char *tool_use_id;
    const char *tool_name;
    const char *text;
    const char *signature;
    const char *partial_json;
    const char *stop_reason;
    const char *error_type;
    const char *message;
} forebay_ir_event;

/**
 * An upstream failure: its HTTP `status`, from 100 to 599, its response headers, each name once, its `body` as text,
 * and the wait it asked for when `has_retry_after` is 1, a double as forebay_outcome_argument takes it. First shape
 * ends at `retry_after_milliseconds`.
 */
typedef struct forebay_ir_failure {
    uint32_t size;
    uint32_t status;
    const forebay_pair *headers;
    size_t header_count;
    const char *body;
    uint32_t has_retry_after;
    double retry_after_milliseconds;
} forebay_ir_failure;

#ifdef __cplusplus
}
#endif

#endif
