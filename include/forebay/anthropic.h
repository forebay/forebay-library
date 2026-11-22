#ifndef FOREBAY_PUBLIC_ANTHROPIC_H
#define FOREBAY_PUBLIC_ANTHROPIC_H

/**
 * @file
 * forebay-library's `anthropic` service: the Anthropic Messages wire format to and from the IR of forebay/ir.h. It
 * keeps no state but a stream's, held by the handle the caller opens and releases. A size-prefixed struct is read only
 * up to its `size`; an array element has no size and never grows.
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
#include "forebay/pair.h"
#include "spisor/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A body to read: `bytes` of `length`, which may be NULL only when `length` is 0. `headers` are the client's headers
 * `decode_request` forwards, a later pair with a name already given replacing the earlier one, names compared ignoring
 * case; `decode_response` ignores them. First shape ends at `length`.
 */
typedef struct forebay_anthropic_body_argument {
    uint32_t size;
    const uint8_t *bytes;
    size_t length;
    const forebay_pair *headers;
    size_t header_count;
} forebay_anthropic_body_argument;

/**
 * The request a body held, the provider's until release_request, and the client's `forwarded_headers`, each name
 * lower cased and once. A body that is no JSON object holds no `request`: `reply_kind` is
 * FOREBAY_REPLY_INVALID_REQUEST, and `status` and `message` are the reply to answer with, static text. First shape
 * ends at `storage`.
 */
typedef struct forebay_anthropic_request_answer {
    uint32_t size;
    const forebay_ir_request *request;
    void *storage;
    const forebay_pair *forwarded_headers;
    size_t forwarded_header_count;
    /** A forebay_reply_kind of routing.h, 0 when the body was read. */
    uint32_t reply_kind;
    uint32_t status;
    const char *message;
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
 * Bytes the service wrote, a body of UTF-8 JSON or a stream's SSE text, `bytes` of `length` with a NUL after them, the
 * provider's until release_body. Every operation that writes bytes answers this, so one release serves them all. First
 * shape ends at `storage`.
 */
typedef struct forebay_anthropic_body_answer {
    uint32_t size;
    const uint8_t *bytes;
    size_t length;
    void *storage;
} forebay_anthropic_body_answer;

/**
 * A stream being read, SSE text to IR events, the provider's from stream_decoder_open until release_stream_decoder.
 * It is used by one thread at a time. A feed that answers anything but OK leaves it as it was before that feed, so
 * the same piece may be fed again; stream_decode_finish leaves it empty, ready for a new stream.
 */
typedef struct forebay_anthropic_stream_decoder forebay_anthropic_stream_decoder;

/**
 * A stream being written, IR events to SSE text, the provider's from stream_encoder_open until
 * release_stream_encoder. It is used by one thread at a time, and a feed that answers anything but OK leaves it as it
 * was.
 */
typedef struct forebay_anthropic_stream_encoder forebay_anthropic_stream_encoder;

/**
 * The next piece of a stream for `decoder`: `bytes` of `length`, which may be NULL only when `length` is 0, split
 * anywhere, inside a line, a frame or a CRLF included. Borrowed for the call. First shape ends at `length`.
 */
typedef struct forebay_anthropic_stream_decode_argument {
    uint32_t size;
    forebay_anthropic_stream_decoder *decoder;
    const uint8_t *bytes;
    size_t length;
} forebay_anthropic_stream_decode_argument;

/** The events a feed completed, in order, the provider's until release_events. First shape ends at `storage`. */
typedef struct forebay_anthropic_events_answer {
    uint32_t size;
    const forebay_ir_event *events;
    size_t event_count;
    void *storage;
} forebay_anthropic_events_answer;

/** The next events of a stream for `encoder`, borrowed for the call. First shape ends at `event_count`. */
typedef struct forebay_anthropic_stream_encode_argument {
    uint32_t size;
    forebay_anthropic_stream_encoder *encoder;
    const forebay_ir_event *events;
    size_t event_count;
} forebay_anthropic_stream_encode_argument;

/**
 * An Anthropic error body to write: the `error.type` the wire names, such as `api_error` or `loader_proxy_error`,
 * required and not empty, and the `message`, required, empty allowed. The HTTP status is the caller's to send. First
 * shape ends at `message`.
 */
typedef struct forebay_anthropic_error_argument {
    uint32_t size;
    const char *error_type;
    const char *message;
} forebay_anthropic_error_argument;

/**
 * One model a provider serves, an array element with no size. `id` is required; `name` NULL or empty lists the model
 * under its id; `context` and `output` are the model's token limits when `has_context` and `has_output` are 1, and
 * the profile's defaults otherwise.
 *
 * @implNote Routing's forebay_catalog_model carries no limits, and a model listing needs them, so the listing has its
 * own model type rather than a second catalog that routing would not read.
 */
typedef struct forebay_anthropic_listed_model {
    const char *id;
    const char *name;
    uint32_t has_context;
    uint64_t context;
    uint32_t has_output;
    uint64_t output;
} forebay_anthropic_listed_model;

/** One provider's models in its catalog order, an array element with no size. `models` may be NULL beside a 0 count. */
typedef struct forebay_anthropic_listed_provider {
    const forebay_anthropic_listed_model *models;
    size_t model_count;
} forebay_anthropic_listed_provider;

/**
 * What a `/v1/models` body lists: every provider's models, in argument order, and the profile's default limits for a
 * model that gives none. `providers` may be NULL beside a 0 count. Borrowed for the call. First shape ends at
 * `provider_count`.
 */
typedef struct forebay_anthropic_model_listing_argument {
    uint32_t size;
    uint64_t default_context;
    uint64_t default_output;
    const forebay_anthropic_listed_provider *providers;
    size_t provider_count;
} forebay_anthropic_model_listing_argument;

/**
 * One model to look up in what a `/v1/models/<id>` request is answered from: the same providers and defaults as the
 * listing, and `id`, required, the model id already URL-decoded by the caller. Borrowed for the call. First shape ends
 * at `id`.
 */
typedef struct forebay_anthropic_model_lookup_argument {
    uint32_t size;
    uint64_t default_context;
    uint64_t default_output;
    const forebay_anthropic_listed_provider *providers;
    size_t provider_count;
    const char *id;
} forebay_anthropic_model_lookup_argument;

/**
 * The body of a lookup, the provider's until release_model_lookup: the listed model when `found` is 1 (answer 200), or
 * the `not_found_error` body when it is 0 (answer 404). First shape ends at `storage`.
 */
typedef struct forebay_anthropic_model_lookup_answer {
    uint32_t size;
    uint32_t found;
    const uint8_t *bytes;
    size_t length;
    void *storage;
} forebay_anthropic_model_lookup_answer;

/**
 * An upstream's own 429 to answer the client: its response `upstream_headers`, a later pair with a name already given
 * replacing the earlier one, names compared ignoring case; `reset_at`, epoch milliseconds, when `has_reset_at` is 1;
 * `now`, epoch milliseconds, which both numbers must hold exactly (at most 2^53 in size); and the zone the caller
 * names the reset in: `zone` the short name it read, such as `CEST` or `UTC`, required and not empty, and
 * `offset_minutes` the zone's offset from UTC at the reset instant, within 1440 either way, which the caller reads
 * because C does no zone lookup. Borrowed for the call. First shape ends at `zone`.
 */
typedef struct forebay_anthropic_rate_limit_argument {
    uint32_t size;
    const forebay_pair *upstream_headers;
    size_t upstream_header_count;
    uint32_t has_reset_at;
    double reset_at;
    double now;
    int64_t offset_minutes;
    const char *zone;
} forebay_anthropic_rate_limit_argument;

/**
 * A reply to send as it is: the HTTP `status`, its `headers` each name once, and the `body` as UTF-8 JSON text. The
 * provider's until release_reply. First shape ends at `storage`.
 */
typedef struct forebay_anthropic_reply_answer {
    uint32_t size;
    uint32_t status;
    const forebay_pair *headers;
    size_t header_count;
    const char *body;
    void *storage;
} forebay_anthropic_reply_answer;

/** What a request is to the proxy. Append-only: a published value keeps its number. */
typedef enum forebay_anthropic_route_kind {
    FOREBAY_ANTHROPIC_ROUTE_MESSAGES = 1,
    FOREBAY_ANTHROPIC_ROUTE_MODEL_LIST = 2,
    FOREBAY_ANTHROPIC_ROUTE_MODEL_LOOKUP = 3,
    FOREBAY_ANTHROPIC_ROUTE_PASS_THROUGH = 4
} forebay_anthropic_route_kind;

/** A request's HTTP `method` and `path`, which may hold a query. Borrowed for the call. First shape ends at `path`. */
typedef struct forebay_anthropic_route_argument {
    uint32_t size;
    const char *method;
    const char *path;
} forebay_anthropic_route_argument;

/**
 * What the request is; for a lookup `model_id` is the text after `/v1/models/`, not URL-decoded, the provider's until
 * release_route. First shape ends at `storage`.
 */
typedef struct forebay_anthropic_route_answer {
    uint32_t size;
    /** A forebay_anthropic_route_kind. */
    uint32_t kind;
    const char *model_id;
    void *storage;
} forebay_anthropic_route_answer;

/**
 * The anthropic service. Every operation answers MALFORMED, before it allocates, for an argument or an answer absent
 * or short of its first shape, an answer that is not empty and `bytes` NULL beside a nonzero `length`; MALFORMED,
 * with the refusal saying what to repair, for a body that is not JSON and for an IR that breaks the rule its header
 * states (a required text NULL, an array NULL beside a nonzero count, a block kind the header does not name, a value
 * tree spisor_value_check refuses, which includes every `extensions` record and an unknown block's `raw`); and
 * OUT_OF_MEMORY, with the answer empty. JSON this library cannot read, a string holding a NUL (`\u0000`), is refused
 * as a body that is not JSON, and a number that is not finite (`1e400`) reads as null, as JSON.stringify writes it.
 * First shape ends at `release_refusal`.
 */
typedef struct forebay_anthropic_definition {
    uint32_t size;
    void *context;
    /**
     * A body to an IR request, as `anthropic-translator`'s decodeRequest does. A string `content` or `system` becomes
     * one text block with `$contentIsString` or `$systemIsString` set. A JSON root that is not an object is answered
     * with the invalid-request reply of the answer; an answer short of the one that holds it is MALFORMED. The
     * argument's headers are answered as the client's `forwarded_headers`, without `authorization`, `x-api-key`,
     * `host`, `content-length`, `accept-encoding`, `connection`, `keep-alive`, `proxy-authenticate`,
     * `proxy-authorization`, `proxy-connection`, `te`, `trailer`, `transfer-encoding` and `upgrade`.
     */
    spisor_status (*decode_request)(void *context, const forebay_anthropic_body_argument *argument,
                                    forebay_anthropic_request_answer *answer, spisor_refusal *refusal);
    /** An IR request to a body, as encodeRequest does, the members in the order it builds them. */
    spisor_status (*encode_request)(void *context, const forebay_anthropic_encode_request_argument *argument,
                                    forebay_anthropic_body_answer *answer, spisor_refusal *refusal);
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
    /**
     * A new, empty stream decoder.
     *
     * @param decoder the consumer's empty handle, freed by `release_stream_decoder`. Required.
     * @return OK; MALFORMED for a NULL `decoder` or one already holding a decoder.
     */
    spisor_status (*stream_decoder_open)(void *context, forebay_anthropic_stream_decoder **decoder,
                                         spisor_refusal *refusal);
    /**
     * Feeds a piece of SSE text and answers the IR events of the frames it completed, as `anthropic-translator`'s
     * stream decoder does: a line ends at LF, one CR before it dropped (a lone CR ends no line); a frame ends at a
     * blank line and is its `data:` lines joined with LF; a comment, any other field, a `ping`, a frame of another
     * type or of no JSON (JSON that cannot be parsed, holds a NUL in a string or is not valid UTF-8, is no JSON here,
     * where a body is refused, and where a TypeScript TextDecoder would read U+FFFD instead), and a delta of another
     * type yield nothing. A message delta keeps its stop sequence in `$stopSequenceRaw`, and a message start's and a
     * message delta's other members are kept in `extensions`. An index that is no count from 0 to 2^53 reads as 0.
     * MALFORMED when `decoder` is NULL.
     */
    spisor_status (*stream_decode)(void *context, const forebay_anthropic_stream_decode_argument *argument,
                                   forebay_anthropic_events_answer *answer, spisor_refusal *refusal);
    /**
     * Feeds a last piece, possibly empty, then ends the stream: an unterminated last line and a frame with no blank
     * line after it yield their event as a complete one would.
     */
    spisor_status (*stream_decode_finish)(void *context, const forebay_anthropic_stream_decode_argument *argument,
                                          forebay_anthropic_events_answer *answer, spisor_refusal *refusal);
    /**
     * A new stream encoder.
     *
     * @param encoder the consumer's empty handle, freed by `release_stream_encoder`. Required.
     * @return OK; MALFORMED for a NULL `encoder` or one already holding an encoder.
     */
    spisor_status (*stream_encoder_open)(void *context, forebay_anthropic_stream_encoder **encoder,
                                         spisor_refusal *refusal);
    /**
     * The SSE text of the events, one `event:` and one `data:` line and a blank line per event, as the translator's
     * stream encoder writes them, with its defaults for what an event leaves NULL. MALFORMED when `encoder` is NULL,
     * for an event of a kind the header does not name, and a content block start whose `block_kind` is not text,
     * tool use or thinking.
     */
    spisor_status (*stream_encode)(void *context, const forebay_anthropic_stream_encode_argument *argument,
                                   forebay_anthropic_body_answer *answer, spisor_refusal *refusal);
    /** Writes the last events, possibly none, then ends the stream; an Anthropic stream needs nothing after them. */
    spisor_status (*stream_encode_finish)(void *context, const forebay_anthropic_stream_encode_argument *argument,
                                          forebay_anthropic_body_answer *answer, spisor_refusal *refusal);
    /**
     * The body of an Anthropic error, `{"type":"error","error":{"type":<error_type>,"message":<message>}}`, with the
     * message escaped as JSON text. MALFORMED, naming the member, for a NULL or empty `error_type` and a NULL
     * `message`.
     */
    spisor_status (*error_envelope)(void *context, const forebay_anthropic_error_argument *argument,
                                    forebay_anthropic_body_answer *answer, spisor_refusal *refusal);
    /**
     * The `/v1/models` body, as the proxy's `model-listing.ts` writes it: every model id once, as the first provider
     * in argument order offers it, each as `type`, `id`, `display_name` (its name, or its id when the name is NULL or
     * empty), `created_at` the fixed 2025-01-01T00:00:00Z, `max_input_tokens` and `max_tokens` (the model's limits, or
     * the defaults), inside `data`, `first_id` and `last_id` (null for no model) and `has_more` false. MALFORMED,
     * naming the member, for a NULL `providers` beside a count, a NULL `models` beside a count and a NULL `id`.
     */
    spisor_status (*model_listing)(void *context, const forebay_anthropic_model_listing_argument *argument,
                                   forebay_anthropic_body_answer *answer, spisor_refusal *refusal);
    /**
     * One model of `/v1/models/<id>`: the model `id` names, built as the listing builds it (first provider wins), with
     * `found` 1, or `{"type":"error","error":{"type":"not_found_error","message":"model not found: <id>"}}` with
     * `found` 0, so the caller answers 200 or 404. MALFORMED, naming the member, as `model_listing` for the providers
     * and for a NULL `id`. An empty `id` is not a lookup: `/v1/models/` belongs to `model_listing`, as the TypeScript
     * routes it, and `model_lookup` answers not found for it.
     */
    spisor_status (*model_lookup)(void *context, const forebay_anthropic_model_lookup_argument *argument,
                                  forebay_anthropic_model_lookup_answer *answer, spisor_refusal *refusal);
    /**
     * The reply to an upstream's own 429, as the proxy's `rateLimitReply` answers it: status 429; the upstream headers
     * without `content-encoding`, `content-length`, `x-hub-rate-limited` and `x-hub-retry-after-ms`; the reset the
     * latest of `reset_at` (0 when absent or negative) and the `anthropic-ratelimit-unified-5h-reset` and
     * `anthropic-ratelimit-unified-reset` headers, each read in whole seconds as `[+-]digits` after trimming ASCII
     * whitespace and times 1000, and ignored otherwise or beyond 2^53 milliseconds; `content-type` set to
     * `application/json` and `retry-after` to the seconds from `now` to the reset, rounded, or 60 when the reset is
     * not after `now`; `anthropic-ratelimit-unified-status` set to `rejected` and `anthropic-ratelimit-unified-reset`
     * to the reset in whole seconds (`now` when there is none) only when the upstream sent none. A header set again
     * keeps its position and one added follows the others. The body is `{"type":"error","error":{"type":
     * "rate_limit_error","message":...}}`, the message "You've hit your usage limit", a middle dot (U+00B7), then
     * " resets at <h:mm AM> <zone>", the time the reset plus `offset_minutes` read as a clock, or " try again later"
     * when the reset is not after `now`. MALFORMED, naming the member, for a NULL `upstream_headers` beside a count, a
     * pair with no name or no value, a `reset_at` or `now` that is not an exact number, an `offset_minutes` out of
     * range and a NULL or empty `zone`.
     */
    spisor_status (*rate_limit_reply)(void *context, const forebay_anthropic_rate_limit_argument *argument,
                                      forebay_anthropic_reply_answer *answer, spisor_refusal *refusal);
    void (*release_request)(void *context, forebay_anthropic_request_answer *answer);
    void (*release_body)(void *context, forebay_anthropic_body_answer *answer);
    void (*release_response)(void *context, forebay_anthropic_response_answer *answer);
    void (*release_events)(void *context, forebay_anthropic_events_answer *answer);
    /** Frees a stream decoder, NULL doing nothing. */
    void (*release_stream_decoder)(void *context, forebay_anthropic_stream_decoder *decoder);
    /** Frees a stream encoder, NULL doing nothing. */
    void (*release_stream_encoder)(void *context, forebay_anthropic_stream_encoder *encoder);
    /** Frees a lookup answer, NULL or one never filled doing nothing. */
    void (*release_model_lookup)(void *context, forebay_anthropic_model_lookup_answer *answer);
    /** Frees a rate-limit reply, NULL or one never filled doing nothing. */
    void (*release_reply)(void *context, forebay_anthropic_reply_answer *answer);
    void (*release_refusal)(void *context, spisor_refusal *refusal);
    /**
     * What a request is, as the proxy routes it: a POST (the method compared ignoring case) to `/v1/messages` is
     * MESSAGES; a GET to `/v1/models` or `/v1/models/` is MODEL_LIST; a GET to `/v1/models/<id>` is MODEL_LOOKUP with
     * the id; anything else, a path with its query cut off compared exactly, is PASS_THROUGH. MALFORMED, naming the
     * member, for a NULL or empty `method` and a NULL `path`.
     */
    spisor_status (*route_request)(void *context, const forebay_anthropic_route_argument *argument,
                                   forebay_anthropic_route_answer *answer, spisor_refusal *refusal);
    /** Frees a route answer, NULL or one never filled doing nothing. */
    void (*release_route)(void *context, forebay_anthropic_route_answer *answer);
} forebay_anthropic_definition;

/** The static forebay_anthropic_definition, living until the library is unloaded. */
FOREBAY_DEFINITION_API const void *forebay_anthropic_definition_of(void);

#ifdef __cplusplus
}
#endif

#endif
