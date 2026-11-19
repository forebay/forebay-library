#ifndef FOREBAY_TEST_IR_JSON_READER_H
#define FOREBAY_TEST_IR_JSON_READER_H

#include <stddef.h>
#include <stdint.h>

#include "ir_json.h"

/**
 * One read in progress: everything goes into `arena`, and `status` holds the first failure, after which every
 * reading function does nothing and answers NULL or zero, so a reader checks once at its end.
 */
typedef struct ir_reader {
    ir_json_arena *arena;
    spisor_status status;
} ir_reader;

typedef enum ir_presence {
    IR_OPTIONAL = 0,
    IR_REQUIRED = 1
} ir_presence;

/** Records MALFORMED, naming the member, unless an earlier failure is already held. */
void ir_read_malformed(ir_reader *reader, const char *key, const char *why);

/** `count` zeroed elements of `size` bytes from the arena; NULL for none or when the reader has failed. */
void *ir_read_take(ir_reader *reader, size_t count, size_t size);

/** An object member, NULL when it is absent or null; MALFORMED when `presence` requires it. */
const cJSON *ir_read_member(ir_reader *reader, const cJSON *object, const char *key, ir_presence presence);

/** A copy of `text` in the arena; NULL when the reader has failed. */
const char *ir_read_copy(ir_reader *reader, const char *text);

const char *ir_read_text(ir_reader *reader, const cJSON *object, const char *key, ir_presence presence);

/** A count, `*has` 1 when present: an integer from 0 to 2^53, as JSON holds one exactly. */
void ir_read_count(ir_reader *reader, const cJSON *object, const char *key, ir_presence presence, uint32_t *has,
                   uint64_t *value);

void ir_read_number(ir_reader *reader, const cJSON *object, const char *key, uint32_t *has, double *value);

void ir_read_flag(ir_reader *reader, const cJSON *object, const char *key, ir_presence presence, uint32_t *has,
                  uint32_t *value);

/** An opaque member as a value tree in the arena, null kept as a node of kind NULL; NULL when absent. */
const spisor_value *ir_read_opaque(ir_reader *reader, const cJSON *object, const char *key, ir_presence presence);

/** An array member, NULL when absent or null; MALFORMED when it is not an array. */
const cJSON *ir_read_array(ir_reader *reader, const cJSON *object, const char *key, ir_presence presence);

const forebay_ir_usage *ir_read_usage(ir_reader *reader, const cJSON *object, const char *key);

/** The blocks of a JSON array, NULL with `*count` 0 for an empty one. */
const forebay_ir_block *ir_read_blocks(ir_reader *reader, const cJSON *array, size_t *count);

void ir_read_block_into(ir_reader *reader, const cJSON *json, forebay_ir_block *block);

void ir_read_event_into(ir_reader *reader, const cJSON *json, forebay_ir_event *event);

/** Copies a value tree into the arena; answers OUT_OF_MEMORY or MALFORMED through `status`. */
const spisor_value *ir_json_arena_value(ir_json_arena *arena, const cJSON *json, spisor_status *status);

/** `bytes` zeroed bytes held by the arena, NULL when memory runs out. */
void *ir_json_arena_take(ir_json_arena *arena, size_t bytes);

#endif
