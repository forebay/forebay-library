#include <stdio.h>

#include "anthropic_operations.h"

#define DETAIL_LIMIT 200

spisor_status anthropic_refuse_problem(spisor_refusal *refusal, const anthropic_problem *problem) {
    char detail[DETAIL_LIMIT];
    char fix[DETAIL_LIMIT];
    snprintf(detail, sizeof detail, "%s must be %s", problem->member, problem->rule);
    snprintf(fix, sizeof fix, "correct %s in the IR so that it is %s", problem->member, problem->rule);
    return spisor_refuse_as(refusal, SPISOR_MALFORMED, detail, fix);
}

void anthropic_writer_problem(const anthropic_writer *writer, anthropic_problem *problem) {
    problem->member = writer->member;
    problem->rule = "a well-formed spisor_value";
}

spisor_status anthropic_refuse_bytes(spisor_refusal *refusal) {
    return spisor_refuse_as(refusal, SPISOR_MALFORMED, "bytes is NULL beside a length that is not 0",
                            "set bytes to the bytes, or length to 0");
}
