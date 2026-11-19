#ifndef FOREBAY_PUBLIC_ANTHROPIC_H
#define FOREBAY_PUBLIC_ANTHROPIC_H

/**
 * @file
 * forebay-library's `anthropic` service: the Anthropic Messages wire format to and from the IR of forebay/ir.h. It
 * keeps no state. A size-prefixed struct is read only up to its `size`; an array element has no size and never grows.
 * Bytes the caller hands in are borrowed for the call; what an answer or a refusal points at is the provider's until
 * the release for its kind. An answer the caller never filled, zeroed, is a release that does nothing.
 *
 * Reading is lenient as the wire format is: a member of the wrong type reads as absent, a number past what a count
 * can hold reads as absent, and every member nothing consumed is kept in the `extensions` of its owner, so writing the
 * IR back restores it. A key beginning with `$` in `extensions` is the translator's own note, such as
 * `$contentIsString` or `$thinkingType`, and never reaches a written body, except on a tool and a tool choice, whose
 * extensions are written whole.
 */

#include <stddef.h>
#include <stdint.h>

#include "forebay/definition.h"
#include "forebay/ir.h"
#include "spisor/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A body to read: `bytes` of `length`, which may be NULL only when `length` is 0. First shape ends at `length`. */
typedef struct forebay_anthropic_body_argument {
    uint32_t size;
    const uint8_t *bytes;
    size_t length;
} forebay_anthropic_body_argument;

/** The request a body held, the provider's until release_request. First shape ends at `storage`. */
typedef struct forebay_anthropic_request_answer {
    uint32_t size;
    const forebay_ir_request *request;
    void *storage;
} forebay_anthropic_request_answer;

/** A request to write as a body, borrowed for the call. First shape ends at `request`. */
typedef struct forebay_anthropic_encode_request_argument {
    uint32_t size;
    const forebay_ir_request *request;
} forebay_anthropic_encode_request_argument;

/** The response a body held, the provider's until release_response. First shape ends at `storage`. */
typedef struct forebay_anthropic_response_answer {
    uint32_t size;
    const forebay_ir_response *response;
    void *storage;
} forebay_anthropic_response_answer;

/** A response to write as a body, borrowed for the call. First shape ends at `response`. */
typedef struct forebay_anthropic_encode_response_argument {
    uint32_t size;
    const forebay_ir_response *response;
} forebay_anthropic_encode_response_argument;

/**
 * A body of UTF-8 JSON, `bytes` of `length` with a NUL after them, the provider's until release_body. Every operation
 * that writes a body answers this, so one release serves them all. First shape ends at `storage`.
 */
typedef struct forebay_anthropic_body_answer {
    uint32_t size;
    const uint8_t *bytes;
    size_t length;
    void *storage;
} forebay_anthropic_body_answer;

/**
 * The anthropic service. Every operation answers MALFORMED, before it allocates, for an argument or an answer absent
 * or short of its first shape, an answer that is not empty and `bytes` NULL beside a nonzero `length`; MALFORMED,
 * with the refusal saying what to repair, for a body that is not JSON and for an IR that breaks the rule its header
 * states (a required text NULL, an array NULL beside a nonzero count, a block kind the header does not name, a value
 * tree spisor_value_check refuses, which includes every `extensions` record and an unknown block's `raw`); and
 * OUT_OF_MEMORY, with the answer empty. JSON this library cannot read, a string holding a NUL (`\u0000`), is refused
 * as a body that is not JSON, and a number that is not finite (`1e400`) reads as null, as JSON.stringify writes it.
 * First shape ends at `release_refusal`; the response operations follow it, and a later operation is appended after
 * those.
 */
typedef struct forebay_anthropic_definition {
    uint32_t size;
    void *context;
    /**
     * A body to an IR request, as `anthropic-translator`'s decodeRequest does. A string `content` or `system` becomes
     * one text block with `$contentIsString` or `$systemIsString` set. A JSON root that is not an object reads as a
     * request with model "", no messages and `stream` 0.
     */
    spisor_status (*decode_request)(void *context, const forebay_anthropic_body_argument *argument,
                                    forebay_anthropic_request_answer *answer, spisor_refusal *refusal);
    /** An IR request to a body, as encodeRequest does, the members in the order it builds them. */
    spisor_status (*encode_request)(void *context, const forebay_anthropic_encode_request_argument *argument,
                                    forebay_anthropic_body_answer *answer, spisor_refusal *refusal);
    void (*release_request)(void *context, forebay_anthropic_request_answer *answer);
    void (*release_body)(void *context, forebay_anthropic_body_answer *answer);
    void (*release_refusal)(void *context, spisor_refusal *refusal);
    /**
     * A body to an IR response, as decodeResponse does. The stop reason is a known one as it is, any other or none
     * "error", and the wire's own is kept in `$stopReasonRaw`, with `$stopSequenceRaw`, `$typeRaw` and `$roleRaw`
     * beside it, so writing restores them. A JSON root that is not an object reads as a response with id and model
     * "", stop reason "error", no blocks, no usage and no extensions.
     */
    spisor_status (*decode_response)(void *context, const forebay_anthropic_body_argument *argument,
                                     forebay_anthropic_response_answer *answer, spisor_refusal *refusal);
    /** An IR response to a body, as encodeResponse does, the members in the order it builds them. */
    spisor_status (*encode_response)(void *context, const forebay_anthropic_encode_response_argument *argument,
                                     forebay_anthropic_body_answer *answer, spisor_refusal *refusal);
    void (*release_response)(void *context, forebay_anthropic_response_answer *answer);
} forebay_anthropic_definition;

/** The static forebay_anthropic_definition, living until the library is unloaded. */
FOREBAY_DEFINITION_API const void *forebay_anthropic_definition_of(void);

#ifdef __cplusplus
}
#endif

#endif
