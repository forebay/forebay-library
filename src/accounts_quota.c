#include "accounts_quota.h"

#include "definition_support.h"

#define CAPACITY_SHAPE SPISOR_SHAPE_THROUGH(forebay_accounts_capacity_argument, count)
#define CAPACITY_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_accounts_capacity_answer, capacity)

/** QuotaHealth.hasCapacity: a pool counts when its remainingFraction is a number above zero. */
static int has_remaining(const spisor_value *pool) {
    const spisor_value *fraction = spisor_value_member(pool, "remainingFraction", sizeof "remainingFraction" - 1);
    return fraction != NULL && fraction->kind == SPISOR_VALUE_NUMBER && fraction->number > 0;
}

static int any_pool_has_capacity(const spisor_value *pools, size_t count) {
    for (size_t index = 0; index < count; index++) {
        if (has_remaining(&pools[index])) {
            return 1;
        }
    }
    return 0;
}

static spisor_status check_pools(const forebay_accounts_capacity_argument *argument, spisor_refusal *refusal) {
    spisor_value list = {SPISOR_VALUE_LIST, 0, 0, NULL, 0, argument->pools, NULL, argument->count};
    if (argument->pools == NULL && argument->count != 0) {
        return forebay_refuse_member(refusal, "pools", "an array of count values");
    }
    spisor_status status = argument->count == 0 ? SPISOR_OK : spisor_value_check(&list);
    if (status == SPISOR_MALFORMED) {
        return forebay_refuse_member(refusal, "pools", "well-formed spisor_values");
    }
    return status;
}

spisor_status accounts_has_capacity(void *context, const forebay_accounts_capacity_argument *argument,
                                    forebay_accounts_capacity_answer *answer, spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, CAPACITY_SHAPE) || answer == NULL ||
        !spisor_has_shape(answer->size, CAPACITY_ANSWER_SHAPE)) {
        return spisor_refuse_shape(refusal, "accounts' has_capacity");
    }
    spisor_status status = check_pools(argument, refusal);
    if (status == SPISOR_OK) {
        answer->capacity = (uint32_t)any_pool_has_capacity(argument->pools, argument->count);
    }
    return status;
}
