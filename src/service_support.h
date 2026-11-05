#ifndef SERVICE_SUPPORT_H
#define SERVICE_SUPPORT_H

#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "spisor/json_kind.h"

#if defined(_WIN32)
#define SERVICE_EXPORT __declspec(dllexport)
#define SERVICE_WINDOWS 1
#else
#define SERVICE_EXPORT __attribute__((visibility("default")))
#define SERVICE_WINDOWS 0
#endif

int32_t service_out_of_memory(uint8_t **out, size_t *out_len);

/** A semantic refusal, answered as a successful call: {"refused": cause, "fix": fix}. */
int32_t service_refuse(const char *cause, const char *fix, uint8_t **out, size_t *out_len);

/** Answers result and consumes it; NULL answers out of memory. */
int32_t service_answer(cJSON *result, uint8_t **out, size_t *out_len);

/** Fails a malformed call as "<subject> <problem>", the standard failure object. */
int32_t service_fail(const char *subject, const char *problem, const char *fix, uint8_t **out, size_t *out_len);

/** Parses a plain call whose argument must be an object. */
int32_t service_open(const uint8_t *in, size_t in_len, spisor_call *call, uint8_t **out, size_t *out_len);

/** Reads the named argument member of the type asked for; answers 0 with the call already failed. */
int service_require(const spisor_call *call, const char *name, int (*is_type)(const cJSON *), const char *type_name,
                    const cJSON **member, uint8_t **out, size_t *out_len, int32_t *status);

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
