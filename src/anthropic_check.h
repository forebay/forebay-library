#ifndef ANTHROPIC_CHECK_H
#define ANTHROPIC_CHECK_H

#include "forebay/ir.h"

/** The first rule an IR value breaks, the member as the header names it and the rule as the member should hold. */
typedef struct anthropic_problem {
    const char *member;
    const char *rule;
} anthropic_problem;

/**
 * Whether an IR request keeps the rules of forebay/ir.h that writing it needs: the first shape reached, a required text
 * or value present, an array present beside a nonzero count, a block kind the header names and an unknown block's
 * `raw` a record. It allocates nothing. Whether a value tree is well formed is left to the writer, which meets it.
 *
 * @return 1, or 0 with `*problem` set.
 */
int anthropic_check_request(const forebay_ir_request *request, anthropic_problem *problem);

#endif
