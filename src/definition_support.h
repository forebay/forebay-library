#ifndef FOREBAY_DEFINITION_SUPPORT_H
#define FOREBAY_DEFINITION_SUPPORT_H

#include "cJSON.h"
#include "spisor/definition.h"
#include "spisor/status.h"
#include "spisor/value.h"

/** Whether a text is present and not empty. */
int forebay_is_text(const char *text);

/** Whether a double is a number JSON can hold: neither infinite nor NaN. */
int forebay_is_finite(double number);

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
