#ifndef FOREBAY_TEST_IR_JSON_H
#define FOREBAY_TEST_IR_JSON_H

/**
 * @file
 * IR JSON, as `contracts/ir.schema.json` and the vectors spell it, to forebay/ir.h's structs and back. Test code only:
 * the link graph refuses a shipped target that reaches it, so the library's own modules never pass IR as JSON.
 *
 * A reader takes a tree from spisor_parse_json_reporting (spisor_vector_read gives one), since spisor_value_of_json
 * needs a tree whose strings hold no NUL. Every text, array and value tree it fills is copied into the arena, so the
 * structs outlive the cJSON tree and live until ir_json_arena_release. A reader answers OK; MALFORMED, having printed
 * the member to stderr, for JSON the structs cannot hold, such as a required member absent, a member of the wrong
 * kind or a kind the header does not name; and OUT_OF_MEMORY. A null member reads as an absent one, except under the
 * five opaque keys, whose value is kept exactly, a null included.
 *
 * A writer answers OK with a new cJSON tree the caller frees with cJSON_Delete, writing a NULL text, an absent count
 * and a NULL pointer as no member at all; MALFORMED for a kind the header does not name or a value tree
 * spisor_value_check refuses; OUT_OF_MEMORY. `*json` is NULL whenever the answer is not OK. Every allocation goes
 * through the library's allocator, so a counting allocator fails these too.
 */

#include <stddef.h>

#include "cJSON.h"
#include "forebay/ir.h"
#include "spisor/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ir_json_chunk ir_json_chunk;

/** What readers allocated, freed together. An empty arena is all zero. */
typedef struct ir_json_arena {
    ir_json_chunk *chunks;
} ir_json_arena;

/** Frees every chunk and value tree the arena holds and leaves it empty; it never allocates. */
void ir_json_arena_release(ir_json_arena *arena);

spisor_status ir_json_read_request(const cJSON *json, ir_json_arena *arena, forebay_ir_request *request);
spisor_status ir_json_read_response(const cJSON *json, ir_json_arena *arena, forebay_ir_response *response);
spisor_status ir_json_read_block(const cJSON *json, ir_json_arena *arena, forebay_ir_block *block);
spisor_status ir_json_read_event(const cJSON *json, ir_json_arena *arena, forebay_ir_event *event);
/** A JSON array of events. */
spisor_status ir_json_read_events(const cJSON *json, ir_json_arena *arena, const forebay_ir_event **events,
                                  size_t *count);
spisor_status ir_json_read_failure(const cJSON *json, ir_json_arena *arena, forebay_ir_failure *failure);

spisor_status ir_json_write_request(const forebay_ir_request *request, cJSON **json);
spisor_status ir_json_write_response(const forebay_ir_response *response, cJSON **json);
spisor_status ir_json_write_block(const forebay_ir_block *block, cJSON **json);
spisor_status ir_json_write_event(const forebay_ir_event *event, cJSON **json);
/** The events as a JSON array. */
spisor_status ir_json_write_events(const forebay_ir_event *events, size_t count, cJSON **json);
spisor_status ir_json_write_failure(const forebay_ir_failure *failure, cJSON **json);

/**
 * A copy of `node` in the IR schema's form, the caller's to cJSON_Delete; NULL only when memory runs out. An object
 * member that is null is dropped, except that the five opaque keys `extensions`, `raw`, `input`, `inputSchema` and
 * `metadata` keep their value exactly, nulls inside it and a null value included. A thinking block's null `text`
 * becomes "", an image block's null `mediaType` "", and a tool result block's null `content` []. An array keeps its
 * null items. Nothing else changes.
 *
 * @implNote The anthropic vectors were recorded through the old Java IR serializer, which printed absent members as
 * null and broke the schema's required `text`, `mediaType` and `content` with null; this is exactly the
 * normalisation `anthropic-translator`'s test (`toSchemaForm` and `requiredByTheSchema`) applies, so both ports
 * compare the same values.
 */
cJSON *ir_json_schema_form(const cJSON *node);

#ifdef __cplusplus
}
#endif

#endif
