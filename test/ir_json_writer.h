#ifndef FOREBAY_TEST_IR_JSON_WRITER_H
#define FOREBAY_TEST_IR_JSON_WRITER_H

#include <stddef.h>
#include <stdint.h>

#include "ir_json.h"

/**
 * One write in progress: `status` holds the first failure, after which every writing function does nothing, so a
 * writer checks once at its end and deletes what it built.
 */
typedef struct ir_writer {
    spisor_status status;
} ir_writer;

/** Adds `item` under `key`, deleting it and recording OUT_OF_MEMORY when it is NULL or cannot be added. */
void ir_write_item(ir_writer *writer, cJSON *object, const char *key, cJSON *item);

/** Appends `item`, deleting it and recording OUT_OF_MEMORY when it is NULL or cannot be appended. */
void ir_write_append(ir_writer *writer, cJSON *array, cJSON *item);

/** A new array, NULL with OUT_OF_MEMORY recorded when memory runs out or the writer has failed. */
cJSON *ir_write_array(ir_writer *writer);

/** A new object, NULL with OUT_OF_MEMORY recorded when memory runs out or the writer has failed. */
cJSON *ir_write_object(ir_writer *writer);

/** Writes nothing for NULL. */
void ir_write_text(ir_writer *writer, cJSON *object, const char *key, const char *text);

/** Writes nothing when `has` is 0. */
void ir_write_count(ir_writer *writer, cJSON *object, const char *key, uint32_t has, uint64_t value);

/** Writes nothing when `has` is 0. */
void ir_write_number(ir_writer *writer, cJSON *object, const char *key, uint32_t has, double value);

void ir_write_flag(ir_writer *writer, cJSON *object, const char *key, uint32_t value);

/** Writes nothing for NULL; a node of kind NULL as null. */
void ir_write_opaque(ir_writer *writer, cJSON *object, const char *key, const spisor_value *value);

/** Writes nothing for NULL. */
void ir_write_usage(ir_writer *writer, cJSON *object, const char *key, const forebay_ir_usage *usage);

/** The blocks as a new array, NULL once the writer has failed. */
cJSON *ir_write_blocks(ir_writer *writer, const forebay_ir_block *blocks, size_t count);

/** The block as a new object, NULL once the writer has failed. */
cJSON *ir_write_block_object(ir_writer *writer, const forebay_ir_block *block);

/** The event as a new object, NULL once the writer has failed. */
cJSON *ir_write_event_object(ir_writer *writer, const forebay_ir_event *event);

/** Answers the writer's status, `*json` taking `built` when it is OK and `built` deleted otherwise. */
spisor_status ir_write_finish(ir_writer *writer, cJSON *built, cJSON **json);

#endif
