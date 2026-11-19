#ifndef ANTHROPIC_CHECK_H
#define ANTHROPIC_CHECK_H

#include "forebay/ir.h"
#include "spisor/status.h"

/** The first rule an IR value breaks, the member as the header names it and the rule as the member should hold. */
typedef struct anthropic_problem {
    const char *member;
    const char *rule;
} anthropic_problem;

/**
 * Whether an IR request keeps the rules of forebay/ir.h that writing it needs: the first shape reached, a required text
 * or value present, an array present beside a nonzero count, a block kind the header names and an unknown block's
 * `raw` a record. Every extensions record and every unknown block's `raw` is walked with spisor_value_check, so
 * nothing later reads an entry, key or text that is not there.
 *
 * @return OK; MALFORMED with `*problem` set; OUT_OF_MEMORY when the walk of a value tree cannot run.
 */
spisor_status anthropic_check_request(const forebay_ir_request *request, anthropic_problem *problem);

/**
 * Whether an IR response keeps the rules of forebay/ir.h that writing it needs, as anthropic_check_request does for a
 * request: `id`, `model` and `stop_reason` set, and the same walk of its blocks and `extensions`.
 */
spisor_status anthropic_check_response(const forebay_ir_response *response, anthropic_problem *problem);

/**
 * Whether IR events keep the rules of forebay/ir.h that writing them needs: present beside a nonzero count, a kind the
 * header names, a content block start's `block_kind` one of text, tool use or thinking, and every `extensions` record
 * walked with spisor_value_check.
 */
spisor_status anthropic_check_events(const forebay_ir_event *events, size_t count, anthropic_problem *problem);

#endif
