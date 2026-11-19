#ifndef ANTHROPIC_READER_H
#define ANTHROPIC_READER_H

#include <stddef.h>

#include "cJSON.h"
#include "spisor/value.h"

/**
 * One read of a wire body into an answer's storage chain. Once memory has run out `out_of_memory` is set, every later
 * allocation answers NULL, and the caller discards the whole chain, so a reader checks the flag once at its end.
 */
typedef struct anthropic_reader {
    void **storage;
    int out_of_memory;
} anthropic_reader;

/** `count` zeroed elements; NULL for none and once memory has run out. */
void *anthropic_take(anthropic_reader *reader, size_t count, size_t size);

/** A copy of `text`; NULL for NULL and once memory has run out. */
const char *anthropic_copy_text(anthropic_reader *reader, const char *text);

/**
 * Copies parsed JSON into `target`, which the parser guarantees holds no NUL in a string and repeats no key.
 * Nothing is written once memory has run out.
 */
void anthropic_fill_value(anthropic_reader *reader, const cJSON *json, spisor_value *target);

/** A new value copied from parsed JSON; NULL once memory has run out. */
const spisor_value *anthropic_copy_value(anthropic_reader *reader, const cJSON *json);

/** The JSON object as a new value record without the member named `skipped_key`. */
const spisor_value *anthropic_copy_record_without(anthropic_reader *reader, const cJSON *record,
                                                  const char *skipped_key);

/** The value of JSON null. */
const spisor_value *anthropic_null_value(void);

/**
 * The members an owner keeps in its `extensions`, in the order they are put.
 *
 * @implNote A key put again takes the later value at the first position, as assigning a property of a JavaScript
 * object does. Only the translator's own keys can repeat a wire key, so a wire key is compared against those alone.
 */
typedef struct anthropic_extension_list {
    spisor_entry *entries;
    size_t count;
    size_t capacity;
    size_t translator_count;
} anthropic_extension_list;

/**
 * An empty list with room for `translator_keys` of the translator's own members and `wire_members` more. A put
 * past that room is dropped, so `translator_keys` counts every translator key the owner's decoder can put.
 */
void anthropic_extensions_begin(anthropic_reader *reader, anthropic_extension_list *list, size_t translator_keys,
                                size_t wire_members);

void anthropic_extensions_put_flag(anthropic_reader *reader, anthropic_extension_list *list, const char *key);

/** Puts the JSON as a value; a NULL item puts null. */
void anthropic_extensions_put_json(anthropic_reader *reader, anthropic_extension_list *list, const char *key,
                                   const cJSON *json);

/** Puts a copy of `value`, which holds the same storage as the original. */
void anthropic_extensions_put_value(anthropic_reader *reader, anthropic_extension_list *list, const char *key,
                                    const spisor_value *value);

/** Puts every member of `wire` whose key is not among the `consumed_count` keys of `consumed`. */
void anthropic_extensions_put_unconsumed(anthropic_reader *reader, anthropic_extension_list *list, const cJSON *wire,
                                         const char *const *consumed, size_t consumed_count);

/** A new record of what the list holds, NULL for an empty list. */
const spisor_value *anthropic_extensions_finish(anthropic_reader *reader, const anthropic_extension_list *list);

#endif
