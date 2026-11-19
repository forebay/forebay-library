#ifndef SERVICE_SUPPORT_H
#define SERVICE_SUPPORT_H

#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"

#if defined(_WIN32)
#define SERVICE_WINDOWS 1
#else
#define SERVICE_WINDOWS 0
#endif

#define SERVICE_SUCCESS 0
#define SERVICE_MALFORMED 1
#define SERVICE_OUT_OF_MEMORY 2

/**
 * What one of routing's decisions answered, over the cJSON tree its definition built from the caller's structs: a
 * result it owns on SUCCESS, which may be a refusal {"refused", "fix"}, or the reason the call is malformed.
 */
typedef struct service_reply {
    int32_t status;
    cJSON *result;
    char message[320];
    char fix[256];
} service_reply;

/** The argument a decision reads, borrowed from its definition for the call. */
typedef struct service_call {
    const cJSON *argument;
} service_call;

int32_t service_out_of_memory(service_reply *reply);

/** A semantic refusal, answered as a successful call: {"refused": cause, "fix": fix}. */
int32_t service_refuse(const char *cause, const char *fix, service_reply *reply);

/** Answers result and consumes it; NULL answers out of memory. */
int32_t service_answer(cJSON *result, service_reply *reply);

/** Fails a malformed call as "<subject> <problem>". */
int32_t service_fail(const char *subject, const char *problem, const char *fix, service_reply *reply);

/** Reads the named argument member of the type asked for; answers 0 with the call already failed. */
int service_require(const service_call *call, const char *name, int (*is_type)(const cJSON *), const char *type_name,
                    const cJSON **member, service_reply *reply, int32_t *status);

/** service_require for a member of a nested object, failed as "<subject> <name> is missing or is not <type>". */
int service_require_member(const cJSON *object, const char *subject, const char *name, int (*is_type)(const cJSON *),
                           const char *type_name, const cJSON **member, service_reply *reply, int32_t *status);

/** {name: item} with item consumed, or NULL when memory runs out. */
cJSON *service_wrap(const char *name, cJSON *item);

int service_add(cJSON *object, const char *name, cJSON *item);
int service_push(cJSON *array, cJSON *item);

/** Whether needle occurs in haystack, comparing ASCII letters without regard to case. */
int service_contains_ignoring_case(const char *haystack, const char *needle);

int service_is_object(const cJSON *value);
int service_is_array(const cJSON *value);
int service_is_string(const cJSON *value);
int service_is_number(const cJSON *value);
int service_is_boolean(const cJSON *value);

#endif
