#ifndef ANTHROPIC_WRITER_H
#define ANTHROPIC_WRITER_H

#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "spisor/status.h"
#include "spisor/value.h"

/**
 * One wire body being built. `status` holds the first failure, OUT_OF_MEMORY or MALFORMED for a value tree that
 * spisor_value_check refuses, with `member` naming that value; after it every function adds nothing, and the caller
 * deletes what was built.
 */
typedef struct anthropic_writer {
    spisor_status status;
    const char *member;
} anthropic_writer;

/** A new object, NULL once the writer has failed. */
cJSON *anthropic_new_object(anthropic_writer *writer);

/** A new array, NULL once the writer has failed. */
cJSON *anthropic_new_array(anthropic_writer *writer);

/** Appends `item`, which it consumes. A NULL item is memory that ran out while it was built. */
void anthropic_append(anthropic_writer *writer, cJSON *array, cJSON *item);

/**
 * Sets a member to `item`, which it consumes, replacing one of the same key at its position. A NULL item is memory
 * that ran out while it was built.
 */
void anthropic_set(anthropic_writer *writer, cJSON *object, const char *key, cJSON *item);

void anthropic_set_text(anthropic_writer *writer, cJSON *object, const char *key, const char *text);
/** Sets a member to `text`, or to null when it is NULL. */
void anthropic_set_text_or_null(anthropic_writer *writer, cJSON *object, const char *key, const char *text);
void anthropic_set_count(anthropic_writer *writer, cJSON *object, const char *key, uint64_t count);
void anthropic_set_number(anthropic_writer *writer, cJSON *object, const char *key, double number);
void anthropic_set_flag(anthropic_writer *writer, cJSON *object, const char *key, uint32_t flag);

/**
 * A value as new JSON, NULL once the writer has failed; a value spisor_value_check refuses fails it, naming `member`.
 */
cJSON *anthropic_json_of(anthropic_writer *writer, const spisor_value *value, const char *member);

/** Sets a member to a value, `member` naming it should it be refused. */
void anthropic_set_value(anthropic_writer *writer, cJSON *object, const char *key, const spisor_value *value,
                         const char *member);

/** Sets every member of a record, in order; anything else sets none. */
void anthropic_set_record(anthropic_writer *writer, cJSON *object, const spisor_value *record, const char *member);

/** The value when it is of kind `kind`, else NULL. */
const spisor_value *anthropic_value_of_kind(const spisor_value *value, spisor_value_kind kind);

/** The member of an extensions record, NULL for none or when `extensions` is not a record. */
const spisor_value *anthropic_extension_of(const spisor_value *extensions, const char *key);

/** Whether the extension is the boolean true. */
int anthropic_extension_flag_is_set(const spisor_value *extensions, const char *key);

#endif
