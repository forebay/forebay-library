#ifndef FOREBAY_DEFINITION_SUPPORT_H
#define FOREBAY_DEFINITION_SUPPORT_H

#include <stddef.h>

#include "cJSON.h"
#include "forebay/pair.h"
#include "spisor/definition.h"
#include "spisor/status.h"
#include "spisor/value.h"

/** Whether a text is present and not empty. */
int forebay_is_text(const char *text);

/** Whether a double is a number JSON can hold: neither infinite nor NaN. */
int forebay_is_finite(double number);

/** 2^53, the largest magnitude a double holds exactly as an integer, which JSON.stringify also prints in full. */
#define FOREBAY_EXACT_LIMIT 9007199254740992.0

/** MALFORMED, naming `member`, for milliseconds that are not finite or are past FOREBAY_EXACT_LIMIT in size. */
spisor_status forebay_check_exact_milliseconds(double milliseconds, const char *member, spisor_refusal *refusal);

/**
 * MALFORMED, naming `member`, for `count` pairs at `pairs` that are NULL beside a count, `count_member` naming that
 * count, or among which a pair carries no name or no value; else OK.
 */
spisor_status forebay_check_pairs(const forebay_pair *pairs, size_t count, const char *member,
                                  const char *count_member, spisor_refusal *refusal);

/** The MALFORMED refusal for a member, named as the struct names it, that breaks the rule `rule`. */
spisor_status forebay_refuse_member(spisor_refusal *refusal, const char *member, const char *rule);

/**
 * A value tree as a new cJSON tree for the store or the logic that reads it, refusing as MALFORMED, naming `member`,
 * a value spisor_value_check refuses.
 *
 * @return OK with `*json` set; MALFORMED or OUT_OF_MEMORY with `*json` NULL.
 */
spisor_status forebay_json_of_member(const spisor_value *value, const char *member, cJSON **json,
                                     spisor_refusal *refusal);

/** Overwrites every string and key of a JSON tree, then deletes it, as the owner of a tree that may hold a secret. */
void forebay_delete_overwritten(cJSON *json);

#endif
