#ifndef FOREBAY_DEFINITION_SUPPORT_H
#define FOREBAY_DEFINITION_SUPPORT_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cJSON.h"
#include "spisor/status.h"
#include "spisor/value.h"

/** The bytes of a size-prefixed struct up to and including `field`, the field its first shape ends at. */
#define FOREBAY_SHAPE_THROUGH(type, field) (offsetof(type, field) + sizeof(((type *)0)->field))

/** Whether a consumer's struct is present and reaches through its first shape of `shape` bytes. */
#define FOREBAY_HAS_SHAPE(given, shape) ((given) != NULL && forebay_has_shape((given)->size, (shape)))

/** Whether a consumer's answer can be filled: present, reaching through its first shape, and holding no storage. */
#define FOREBAY_IS_EMPTY_ANSWER(answer, shape) (FOREBAY_HAS_SHAPE(answer, shape) && (answer)->storage == NULL)

/**
 * FOREBAY_IS_EMPTY_ANSWER for an answer holding an array, whose count must be zero too.
 *
 * @implNote A fill appends at its array's count, so a nonzero count beside NULL storage would write past the array.
 */
#define FOREBAY_IS_EMPTY_COUNTED_ANSWER(answer, shape, count) (FOREBAY_IS_EMPTY_ANSWER(answer, shape) && (count) == 0)

/** Zeroes every field of an answer after its `size`, through its first shape of `shape` bytes. */
#define FOREBAY_CLEAR_ANSWER(answer, shape) \
    memset((char *)(answer) + sizeof((answer)->size), 0, (shape) - sizeof((answer)->size))

int forebay_has_shape(uint32_t size, size_t shape);

/** Whether a text is present and not empty. */
int forebay_is_text(const char *text);

/** Whether a double is a number JSON can hold: neither infinite nor NaN. */
int forebay_is_finite(double number);

/** Fills the refusal and answers `status`, or OUT_OF_MEMORY when the refusal could not be filled. */
spisor_status forebay_refuse_as(spisor_refusal *refusal, spisor_status status, const char *detail, const char *fix);

/** The MALFORMED refusal every definition answers for an argument or answer absent, short or not empty. */
spisor_status forebay_refuse_shape(spisor_refusal *refusal, const char *operation);

/** The MALFORMED refusal for a member, named as the struct names it, that breaks the rule `rule`. */
spisor_status forebay_refuse_member(spisor_refusal *refusal, const char *member, const char *rule);

/** Every definition's release for a refusal it filled. */
void forebay_release_refusal(void *context, spisor_refusal *refusal);

/**
 * A value tree as a new cJSON tree for the store or the logic that reads it, refusing as MALFORMED, naming `member`,
 * a value spisor_value_check refuses.
 *
 * @return OK with `*json` set; MALFORMED or OUT_OF_MEMORY with `*json` NULL.
 */
spisor_status forebay_json_of_member(const spisor_value *value, const char *member, cJSON **json,
                                     spisor_refusal *refusal);

/** Overwrites a secret's bytes before they are freed, through a volatile pointer so no write is optimised away. */
void forebay_overwrite(void *bytes, size_t length);

/** Overwrites every string and key of a JSON tree, then deletes it, as the owner of a tree that may hold a secret. */
void forebay_delete_overwritten(cJSON *json);

#endif
