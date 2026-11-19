#ifndef SERVICE_SUPPORT_H
#define SERVICE_SUPPORT_H

#include <stddef.h>

#include "cJSON.h"
#include "spisor/status.h"

#if defined(_WIN32)
#define SERVICE_WINDOWS 1
#else
#define SERVICE_WINDOWS 0
#endif

/**
 * What one of routing's decisions answered, over the cJSON tree its definition built from the caller's structs: a
 * result it owns on OK, or on MALFORMED and REFUSED the definition's `refusal` filled.
 */
typedef struct service_reply {
    spisor_status status;
    cJSON *result;
    spisor_refusal *refusal;
} service_reply;

/** The argument a decision reads, borrowed from its definition for the call. */
typedef struct service_call {
    const cJSON *argument;
} service_call;

spisor_status service_out_of_memory(service_reply *reply);

/** Refuses the call as REFUSED with `cause` and `fix`, or answers OUT_OF_MEMORY when the refusal cannot be filled. */
spisor_status service_refuse(const char *cause, const char *fix, service_reply *reply);

/** Answers result and consumes it; NULL answers out of memory. */
spisor_status service_answer(cJSON *result, service_reply *reply);

/** Refuses a malformed call as "<subject> <problem>", or answers OUT_OF_MEMORY when the refusal cannot be filled. */
spisor_status service_fail(const char *subject, const char *problem, const char *fix, service_reply *reply);

/** {name: item} with item consumed, or NULL when memory runs out. */
cJSON *service_wrap(const char *name, cJSON *item);

int service_add(cJSON *object, const char *name, cJSON *item);
int service_push(cJSON *array, cJSON *item);

/** Whether needle occurs in haystack, comparing ASCII letters without regard to case. */
int service_contains_ignoring_case(const char *haystack, const char *needle);

#endif
