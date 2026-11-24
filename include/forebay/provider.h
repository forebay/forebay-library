#ifndef FOREBAY_PUBLIC_PROVIDER_H
#define FOREBAY_PUBLIC_PROVIDER_H

/**
 * @file
 * The provider contract: the definition a provider plugin implements, so a proxy calls every provider the same way.
 * forebay-library implements none: `library.json` publishes its description under the capability
 * `forebay.provider`, and a plugin providing that capability answers a forebay_provider_definition of its own. A
 * size-prefixed struct is read only up to its `size`; text the caller hands in is NUL-terminated UTF-8 borrowed for the
 * call; what an answer or a refusal points at is the provider's until the release for its kind.
 */

#include <stddef.h>
#include <stdint.h>

#include "forebay/definition.h"
#include "forebay/ir.h"
#include "forebay/pair.h"
#include "forebay/routing.h"
#include "spisor/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A streamed reply being read, the provider's from the handle answer that carried it until release_stream, apart
 * from that answer. It is read by one thread at a time.
 */
typedef struct forebay_provider_stream forebay_provider_stream;

/** A request to serve and the client's headers to forward. First shape ends at `header_count`. */
typedef struct forebay_provider_handle_argument {
    uint32_t size;
    const forebay_ir_request *request;
    const forebay_pair *headers;
    size_t header_count;
} forebay_provider_handle_argument;

/**
 * How a provider served a request. THROWN is a call that got no upstream response at all, which a proxy reports to
 * routing's `step` as FOREBAY_REPORT_THROWN with its message. Append-only: a published value keeps its number.
 */
typedef enum forebay_provider_handle_kind {
    FOREBAY_PROVIDER_HANDLE_RESPONSE = 1,
    FOREBAY_PROVIDER_HANDLE_FAILURE = 2,
    FOREBAY_PROVIDER_HANDLE_STREAM = 3,
    FOREBAY_PROVIDER_HANDLE_THROWN = 4
} forebay_provider_handle_kind;

/**
 * A whole `response`, the upstream's `failure`, a `stream` to read with `next`, or the `message` of a thrown call, as
 * `kind` says; the members of the other kinds are NULL. First shape ends at `stream`.
 */
typedef struct forebay_provider_handle_answer {
    uint32_t size;
    /** A forebay_provider_handle_kind. */
    uint32_t kind;
    const forebay_ir_response *response;
    const forebay_ir_failure *failure;
    forebay_provider_stream *stream;
    const char *message;
} forebay_provider_handle_answer;

/** What a stream gave next. Append-only: a published value keeps its number. */
typedef enum forebay_provider_next_kind {
    FOREBAY_PROVIDER_NEXT_EVENT = 1,
    FOREBAY_PROVIDER_NEXT_END = 2,
    FOREBAY_PROVIDER_NEXT_FAILURE = 3
} forebay_provider_next_kind;

/**
 * The stream's next `event`, its end, or the `failure` message of a stream that broke, as `kind` says. What it points
 * at is the provider's until the next `next` on the same stream or release_stream. First shape ends at `failure`.
 */
typedef struct forebay_provider_next_answer {
    uint32_t size;
    /** A forebay_provider_next_kind. */
    uint32_t kind;
    const forebay_ir_event *event;
    const char *failure;
} forebay_provider_next_answer;

/**
 * The provider's catalog entry: its name, its models with their limits, its ranking and its default model. First
 * shape ends at `storage`.
 */
typedef struct forebay_provider_models_answer {
    uint32_t size;
    forebay_catalog_provider provider;
    void *storage;
} forebay_provider_models_answer;

/**
 * The provider contract. Every operation answers OK with its answer filled, MALFORMED for an argument or an answer
 * absent or short of its first shape, REFUSED with the refusal saying what to repair, or OUT_OF_MEMORY with the
 * answer empty; `next` takes no refusal, since a stream that broke is an answer of its own, so it answers only OK,
 * MALFORMED or OUT_OF_MEMORY, never REFUSED. First shape ends at `release_refusal`.
 */
typedef struct forebay_provider_definition {
    uint32_t size;
    void *context;
    /** The provider's catalog entry. @blocking it may ask its upstream. */
    spisor_status (*models)(void *context, forebay_provider_models_answer *answer, spisor_refusal *refusal);
    /** One request served; REFUSED only for a call the caller must repair. @blocking it waits for its upstream. */
    spisor_status (*handle)(void *context, const forebay_provider_handle_argument *argument,
                            forebay_provider_handle_answer *answer, spisor_refusal *refusal);
    /** The next of a stream a handle answered. @blocking it waits for its upstream. */
    spisor_status (*next)(void *context, forebay_provider_stream *stream, forebay_provider_next_answer *answer);
    /** Frees a stream, NULL doing nothing, ending it upstream when it has not ended. */
    void (*release_stream)(void *context, forebay_provider_stream *stream);
    void (*release_models_answer)(void *context, forebay_provider_models_answer *answer);
    void (*release_handle_answer)(void *context, forebay_provider_handle_answer *answer);
    void (*release_refusal)(void *context, spisor_refusal *refusal);
} forebay_provider_definition;

#ifdef __cplusplus
}
#endif

#endif
