#ifndef FOREBAY_TEST_ANTHROPIC_CALL_H
#define FOREBAY_TEST_ANTHROPIC_CALL_H

#include "support.h"

#include "forebay/anthropic.h"
#include "forebay/routing.h"
#include "ir_json.h"
#include "spisor/json_kind.h"
#include "vector_reader.h"

/**
 * @file
 * The anthropic service called through its definition, as a consumer calls it, with the IR and the wire JSON of the
 * vectors turned into the structs and bytes it takes and compared after the same schema form `ir_codec_test` applies.
 * A decode vector's `input` is the wire body, printed to bytes; an encode vector's `input` is the IR, read by the
 * test-only IR codec. A wire-shape vector's `input` is the operation's argument as JSON: `replyKind` (a
 * forebay_reply_kind by its vector name, such as `proxyError`) and `message`, or `models`, a list of the entries
 * routing's `model_entries` answers, each with `id`, `name`, `context` and `output`, and for a lookup the `id`.
 * A rate-limit vector's `input` is `upstreamHeaders`, `resetAt`, `now`, `offsetMinutes`, `zoneEnUs` and `zoneEnGb`,
 * and its `output` the `status`, the `headers` in order and the `body` text of the reply. A `decodeRequestWithHeaders`
 * vector's `input` is the wire `body` as JSON, or the text `rawBody` as it is, and the client's `headers`, and its
 * `output` the `forwardedHeaders` in order and either the
 * `model` of the request read or the `reply` (`kind`, `status`, `message`) of a body that is no object. A
 * `routeRequest` vector's `input` is the `method` and `path`, and its `output` the `kind` (`messages`, `modelList`,
 * `modelLookup` or `passThrough`), the `modelId` of a lookup and the `keyHeaders` of every route. A `resetTime`
 * vector's `input` is the upstream `headers` and `now`, and its `output` `hasResetAt` and `resetAt`, 0 when there is
 * none.
 */

static inline const forebay_anthropic_definition *anthropic_definition(void) {
    return (const forebay_anthropic_definition *)forebay_anthropic_definition_of();
}

/** The IR request of an encode vector's `input` or any IR JSON, in the arena; 1 when the codec read it. */
static inline int anthropic_read_ir(const cJSON *ir, ir_json_arena *arena, forebay_ir_request *request) {
    cJSON *form = ir_json_schema_form(ir);
    spisor_status status = form == NULL ? SPISOR_OUT_OF_MEMORY : ir_json_read_request(form, arena, request);
    cJSON_Delete(form);
    return status == SPISOR_OK;
}

static inline spisor_status anthropic_decode_text(const char *text, forebay_anthropic_request_answer *answer,
                                                  spisor_refusal *refusal) {
    forebay_anthropic_body_argument argument = SPISOR_INIT(forebay_anthropic_body_argument);
    argument.bytes = (const uint8_t *)text;
    argument.length = strlen(text);
    return anthropic_definition()->decode_request(NULL, &argument, answer, refusal);
}

static inline spisor_status anthropic_encode_request_of(const forebay_ir_request *request,
                                                        forebay_anthropic_body_answer *answer,
                                                        spisor_refusal *refusal) {
    forebay_anthropic_encode_request_argument argument = SPISOR_INIT(forebay_anthropic_encode_request_argument);
    argument.request = request;
    return anthropic_definition()->encode_request(NULL, &argument, answer, refusal);
}

/** Whether the IR JSON `written`, in the schema form, is the IR JSON `expected` in its schema form. */
static inline int anthropic_ir_json_equals(const cJSON *written, const cJSON *expected) {
    cJSON *written_form = ir_json_schema_form(written);
    cJSON *expected_form = ir_json_schema_form(expected);
    int same = written_form != NULL && expected_form != NULL && cJSON_Compare(expected_form, written_form, 1);
    if (!same && written_form != NULL && expected_form != NULL) {
        char *wanted = cJSON_PrintUnformatted(expected_form);
        char *got = cJSON_PrintUnformatted(written_form);
        fprintf(stderr, "  expected %s\n  actual   %s\n", wanted, got);
        cJSON_free(wanted);
        cJSON_free(got);
    }
    cJSON_Delete(written_form);
    cJSON_Delete(expected_form);
    return same;
}

/** Whether an IR request, in the schema form, is the IR JSON `expected` in its schema form. */
static inline int anthropic_request_equals(const forebay_ir_request *request, const cJSON *expected) {
    cJSON *written = NULL;
    int same = ir_json_write_request(request, &written) == SPISOR_OK && anthropic_ir_json_equals(written, expected);
    cJSON_Delete(written);
    return same;
}

/** Whether an IR response, in the schema form, is the IR JSON `expected` in its schema form. */
static inline int anthropic_response_equals(const forebay_ir_response *response, const cJSON *expected) {
    cJSON *written = NULL;
    int same = ir_json_write_response(response, &written) == SPISOR_OK && anthropic_ir_json_equals(written, expected);
    cJSON_Delete(written);
    return same;
}

/** The IR response of an encode vector's `input` or any IR JSON, in the arena; 1 when the codec read it. */
static inline int anthropic_read_response_ir(const cJSON *ir, ir_json_arena *arena, forebay_ir_response *response) {
    cJSON *form = ir_json_schema_form(ir);
    spisor_status status = form == NULL ? SPISOR_OUT_OF_MEMORY : ir_json_read_response(form, arena, response);
    cJSON_Delete(form);
    return status == SPISOR_OK;
}

static inline spisor_status anthropic_decode_response_text(const char *text,
                                                           forebay_anthropic_response_answer *answer,
                                                           spisor_refusal *refusal) {
    forebay_anthropic_body_argument argument = SPISOR_INIT(forebay_anthropic_body_argument);
    argument.bytes = (const uint8_t *)text;
    argument.length = strlen(text);
    return anthropic_definition()->decode_response(NULL, &argument, answer, refusal);
}

static inline spisor_status anthropic_encode_response_of(const forebay_ir_response *response,
                                                         forebay_anthropic_body_answer *answer,
                                                         spisor_refusal *refusal) {
    forebay_anthropic_encode_response_argument argument = SPISOR_INIT(forebay_anthropic_encode_response_argument);
    argument.response = response;
    return anthropic_definition()->encode_response(NULL, &argument, answer, refusal);
}

/** Whether a body, parsed as JSON, equals the wire JSON `expected`. */
static inline int anthropic_body_equals(const forebay_anthropic_body_answer *answer, const cJSON *expected) {
    int out_of_memory = 0;
    cJSON *parsed = spisor_parse_json_reporting(answer->bytes, answer->length, &out_of_memory);
    int same = parsed != NULL && cJSON_Compare(parsed, expected, 1);
    if (!same) {
        char *wanted = cJSON_PrintUnformatted(expected);
        fprintf(stderr, "  expected %s\n  actual   %.*s\n", wanted, (int)answer->length, (const char *)answer->bytes);
        cJSON_free(wanted);
    }
    cJSON_Delete(parsed);
    return same;
}

/** One feed of a stream's text: `length` bytes at `bytes`. */
typedef struct anthropic_piece {
    const char *bytes;
    size_t length;
} anthropic_piece;

/**
 * What a direction is called on: a decode's wire text, a stream decode's whole text `joined` and the pieces it is fed
 * in, or an encode's IR, a stream encode's events among it, read into the arena.
 */
typedef struct anthropic_input {
    const char *text;
    char *printed;
    char *joined;
    size_t joined_length;
    anthropic_piece *pieces;
    size_t piece_count;
    ir_json_arena arena;
    forebay_ir_request request;
    forebay_ir_response response;
    const forebay_ir_event *events;
    size_t event_count;
    forebay_anthropic_error_argument error;
    forebay_anthropic_model_listing_argument listing;
    forebay_catalog_model *models;
    const char *lookup_id;
    forebay_anthropic_rate_limit_argument rate_limit;
    forebay_pair *pairs;
    size_t pair_count;
    forebay_anthropic_route_argument route;
    forebay_anthropic_reset_argument reset;
} anthropic_input;

/** Every answer a whole stream through one handle gave, each feed's and the finish's, held until released. */
typedef struct anthropic_stream_answer {
    uint32_t size;
    forebay_anthropic_events_answer *events;
    forebay_anthropic_body_answer *texts;
    size_t count;
} anthropic_stream_answer;

/** Any answer a direction fills, begun empty with the size of its own kind by anthropic_start_answer. */
typedef union anthropic_answer {
    uint32_t size;
    forebay_anthropic_request_answer request;
    forebay_anthropic_response_answer response;
    forebay_anthropic_body_answer body;
    forebay_anthropic_model_lookup_answer lookup;
    forebay_anthropic_reply_answer reply;
    forebay_anthropic_route_answer route;
    forebay_anthropic_reset_answer reset;
    anthropic_stream_answer stream;
} anthropic_answer;

/**
 * One direction of the service as a vector names it: how its input is read from the vector, the call through the
 * definition, the comparison of what it answered with the vector's `output`, and the release of that answer.
 */
typedef struct anthropic_direction {
    const char *name;
    uint32_t answer_size;
    int (*read_input)(const cJSON *json, anthropic_input *input);
    spisor_status (*call)(const anthropic_input *input, anthropic_answer *answer, spisor_refusal *refusal);
    int (*matches)(const anthropic_answer *answer, const cJSON *expected);
    void (*release)(anthropic_answer *answer);
} anthropic_direction;

/** An input holding `text`, borrowed, as a decode reads it. */
static inline anthropic_input anthropic_input_of_text(const char *text) {
    anthropic_input input = {0};
    input.text = text;
    return input;
}

static inline void anthropic_input_release(anthropic_input *input) {
    cJSON_free(input->printed);
    free(input->joined);
    free(input->pieces);
    free(input->models);
    free(input->pairs);
    ir_json_arena_release(&input->arena);
    *input = (anthropic_input){0};
}

/** Zeroes the answer whole and sets the size of the direction's kind. */
static inline void anthropic_start_answer(const anthropic_direction *direction, anthropic_answer *answer) {
    memset(answer, 0, sizeof *answer);
    answer->size = direction->answer_size;
}

static inline int anthropic_read_wire_text(const cJSON *json, anthropic_input *input) {
    *input = (anthropic_input){0};
    input->printed = cJSON_PrintUnformatted(json);
    input->text = input->printed;
    return input->printed != NULL;
}

static inline int anthropic_read_request_input(const cJSON *json, anthropic_input *input) {
    *input = (anthropic_input){0};
    return anthropic_read_ir(json, &input->arena, &input->request);
}

static inline int anthropic_read_response_input(const cJSON *json, anthropic_input *input) {
    *input = (anthropic_input){0};
    return anthropic_read_response_ir(json, &input->arena, &input->response);
}

static inline spisor_status anthropic_call_decode_request(const anthropic_input *input, anthropic_answer *answer,
                                                          spisor_refusal *refusal) {
    return anthropic_decode_text(input->text, &answer->request, refusal);
}

static inline spisor_status anthropic_call_encode_request(const anthropic_input *input, anthropic_answer *answer,
                                                          spisor_refusal *refusal) {
    return anthropic_encode_request_of(&input->request, &answer->body, refusal);
}

static inline spisor_status anthropic_call_decode_response(const anthropic_input *input, anthropic_answer *answer,
                                                           spisor_refusal *refusal) {
    return anthropic_decode_response_text(input->text, &answer->response, refusal);
}

static inline spisor_status anthropic_call_encode_response(const anthropic_input *input, anthropic_answer *answer,
                                                           spisor_refusal *refusal) {
    return anthropic_encode_response_of(&input->response, &answer->body, refusal);
}

static inline int anthropic_request_matches(const anthropic_answer *answer, const cJSON *expected) {
    return answer->request.request != NULL && anthropic_request_equals(answer->request.request, expected);
}

static inline int anthropic_response_matches(const anthropic_answer *answer, const cJSON *expected) {
    return answer->response.response != NULL && anthropic_response_equals(answer->response.response, expected);
}

static inline int anthropic_body_matches(const anthropic_answer *answer, const cJSON *expected) {
    return anthropic_body_equals(&answer->body, expected);
}

static inline void anthropic_release_request(anthropic_answer *answer) {
    anthropic_definition()->release_request(NULL, &answer->request);
}

static inline void anthropic_release_response(anthropic_answer *answer) {
    anthropic_definition()->release_response(NULL, &answer->response);
}

static inline void anthropic_release_body(anthropic_answer *answer) {
    anthropic_definition()->release_body(NULL, &answer->body);
}

/** Cuts the input's `joined` text into consecutive pieces of the `count` lengths; 0 when memory runs out. */
static inline int anthropic_input_cut(anthropic_input *input, const size_t *lengths, size_t count) {
    free(input->pieces);
    input->pieces = calloc(count == 0 ? 1 : count, sizeof *input->pieces);
    input->piece_count = input->pieces == NULL ? 0 : count;
    size_t offset = 0;
    for (size_t index = 0; index < input->piece_count; index++) {
        input->pieces[index].bytes = input->joined + offset;
        input->pieces[index].length = lengths[index];
        offset += lengths[index];
    }
    return input->pieces != NULL;
}

/** A stream decode vector's input: its text chunks, joined, and fed as the vector lists them. */
static inline int anthropic_read_stream_text(const cJSON *json, anthropic_input *input) {
    *input = (anthropic_input){0};
    if (!cJSON_IsArray(json)) {
        return 0;
    }
    size_t count = (size_t)cJSON_GetArraySize(json);
    size_t *lengths = calloc(count == 0 ? 1 : count, sizeof *lengths);
    if (lengths == NULL) {
        support_fail("a stream text", "the test could not allocate its piece lengths");
        return 0;
    }
    size_t total = 0;
    size_t index = 0;
    const cJSON *chunk = NULL;
    cJSON_ArrayForEach(chunk, json) {
        const char *text = cJSON_GetStringValue(chunk);
        lengths[index] = text == NULL ? 0 : strlen(text);
        total += lengths[index++];
    }
    input->joined = malloc(total + 1);
    if (input->joined == NULL) {
        support_fail("a stream text", "the test could not allocate its joined text");
        free(lengths);
        return 0;
    }
    input->joined_length = total;
    size_t offset = 0;
    index = 0;
    cJSON_ArrayForEach(chunk, json) {
        if (lengths[index] > 0) {
            memcpy(input->joined + offset, cJSON_GetStringValue(chunk), lengths[index]);
        }
        offset += lengths[index++];
    }
    input->joined[total] = '\0';
    int cut = anthropic_input_cut(input, lengths, count);
    free(lengths);
    return cut;
}

/** A stream encode vector's input: its IR events, in the arena. */
static inline int anthropic_read_events_input(const cJSON *json, anthropic_input *input) {
    *input = (anthropic_input){0};
    cJSON *form = ir_json_schema_form(json);
    spisor_status status = form == NULL ? SPISOR_OUT_OF_MEMORY
                                        : ir_json_read_events(form, &input->arena, &input->events, &input->event_count);
    cJSON_Delete(form);
    return status == SPISOR_OK;
}

static inline void anthropic_release_stream_answers(anthropic_answer *answer) {
    anthropic_stream_answer *stream = &answer->stream;
    for (size_t index = 0; stream->events != NULL && index < stream->count; index++) {
        anthropic_definition()->release_events(NULL, &stream->events[index]);
    }
    for (size_t index = 0; stream->texts != NULL && index < stream->count; index++) {
        anthropic_definition()->release_body(NULL, &stream->texts[index]);
    }
    free(stream->events);
    free(stream->texts);
    stream->events = NULL;
    stream->texts = NULL;
    stream->count = 0;
}

/**
 * Feeds one piece, the last when `finish` is 1, and feeds it once more after OUT_OF_MEMORY, which the header says
 * leaves the decoder as it was; so a single failed allocation still decodes the whole stream.
 */
static inline spisor_status anthropic_feed_decoder(forebay_anthropic_stream_decoder *decoder,
                                                   const anthropic_piece *piece, int finish,
                                                   forebay_anthropic_events_answer *answer, spisor_refusal *refusal) {
    forebay_anthropic_stream_decode_argument argument = SPISOR_INIT(forebay_anthropic_stream_decode_argument);
    argument.decoder = decoder;
    argument.bytes = (const uint8_t *)piece->bytes;
    argument.length = piece->length;
    spisor_status status = SPISOR_OUT_OF_MEMORY;
    for (int attempt = 0; attempt < 2 && status == SPISOR_OUT_OF_MEMORY; attempt++) {
        *answer = (forebay_anthropic_events_answer)SPISOR_INIT(forebay_anthropic_events_answer);
        status = finish ? anthropic_definition()->stream_decode_finish(NULL, &argument, answer, refusal)
                        : anthropic_definition()->stream_decode(NULL, &argument, answer, refusal);
        if (status == SPISOR_OUT_OF_MEMORY &&
            (answer->events != NULL || answer->event_count != 0 || answer->storage != NULL)) {
            support_fail("a stream decode", "an out-of-memory feed did not leave its answer empty");
        }
    }
    return status;
}

/** Opens a decoder, feeds it every piece, then finishes it with nothing more, keeping every answer. */
static inline spisor_status anthropic_call_decode_stream(const anthropic_input *input, anthropic_answer *answer,
                                                         spisor_refusal *refusal) {
    static const anthropic_piece nothing = {"", 0};
    forebay_anthropic_stream_decoder *opened = NULL;
    spisor_status status = anthropic_definition()->stream_decoder_open(NULL, &opened, refusal);
    anthropic_stream_answer *stream = &answer->stream;
    stream->events = calloc(input->piece_count + 1, sizeof *stream->events);
    if (stream->events == NULL) {
        support_fail("a stream decode", "the test could not allocate its answers");
        anthropic_definition()->release_stream_decoder(NULL, opened);
        return SPISOR_OUT_OF_MEMORY;
    }
    stream->count = input->piece_count + 1;
    for (size_t index = 0; status == SPISOR_OK && index <= input->piece_count; index++) {
        int finish = index == input->piece_count;
        status = anthropic_feed_decoder(opened, finish ? &nothing : &input->pieces[index], finish,
                                        &stream->events[index], refusal);
    }
    anthropic_definition()->release_stream_decoder(NULL, opened);
    if (status != SPISOR_OK) {
        anthropic_release_stream_answers(answer);
    }
    return status;
}

/** Writes one feed's events, the last when `finish` is 1, once more after OUT_OF_MEMORY, as the decoder's feed is. */
static inline spisor_status anthropic_feed_encoder(forebay_anthropic_stream_encoder *encoder,
                                                   const forebay_ir_event *events, size_t count, int finish,
                                                   forebay_anthropic_body_answer *answer, spisor_refusal *refusal) {
    forebay_anthropic_stream_encode_argument argument = SPISOR_INIT(forebay_anthropic_stream_encode_argument);
    argument.encoder = encoder;
    argument.events = events;
    argument.event_count = count;
    spisor_status status = SPISOR_OUT_OF_MEMORY;
    for (int attempt = 0; attempt < 2 && status == SPISOR_OUT_OF_MEMORY; attempt++) {
        *answer = (forebay_anthropic_body_answer)SPISOR_INIT(forebay_anthropic_body_answer);
        status = finish ? anthropic_definition()->stream_encode_finish(NULL, &argument, answer, refusal)
                        : anthropic_definition()->stream_encode(NULL, &argument, answer, refusal);
        if (status == SPISOR_OUT_OF_MEMORY &&
            (answer->bytes != NULL || answer->length != 0 || answer->storage != NULL)) {
            support_fail("a stream encode", "an out-of-memory feed did not leave its answer empty");
        }
    }
    return status;
}

/** Opens an encoder, writes the events one feed each, as the translator's transform does, then finishes it. */
static inline spisor_status anthropic_call_encode_stream(const anthropic_input *input, anthropic_answer *answer,
                                                         spisor_refusal *refusal) {
    forebay_anthropic_stream_encoder *opened = NULL;
    spisor_status status = anthropic_definition()->stream_encoder_open(NULL, &opened, refusal);
    anthropic_stream_answer *stream = &answer->stream;
    stream->texts = calloc(input->event_count + 1, sizeof *stream->texts);
    if (stream->texts == NULL) {
        support_fail("a stream encode", "the test could not allocate its answers");
        anthropic_definition()->release_stream_encoder(NULL, opened);
        return SPISOR_OUT_OF_MEMORY;
    }
    stream->count = input->event_count + 1;
    for (size_t index = 0; status == SPISOR_OK && index <= input->event_count; index++) {
        int finish = index == input->event_count;
        status = anthropic_feed_encoder(opened, finish ? NULL : &input->events[index], finish ? 0 : 1, finish,
                                        &stream->texts[index], refusal);
    }
    anthropic_definition()->release_stream_encoder(NULL, opened);
    if (status != SPISOR_OK) {
        anthropic_release_stream_answers(answer);
    }
    return status;
}

/** Every event the stream's answers hold, in order, as one array freed with free; NULL when memory runs out. */
static inline forebay_ir_event *anthropic_stream_events(const anthropic_stream_answer *stream, size_t *total) {
    *total = 0;
    for (size_t index = 0; index < stream->count; index++) {
        *total += stream->events[index].event_count;
    }
    forebay_ir_event *events = calloc(*total == 0 ? 1 : *total, sizeof *events);
    size_t filled = 0;
    for (size_t index = 0; events != NULL && index < stream->count; index++) {
        for (size_t event = 0; event < stream->events[index].event_count; event++) {
            events[filled++] = stream->events[index].events[event];
        }
    }
    return events;
}

/** Whether every event the stream's answers hold, in order, is the IR JSON list `expected`. */
static inline int anthropic_stream_events_match(const anthropic_answer *answer, const cJSON *expected) {
    size_t total = 0;
    forebay_ir_event *events = anthropic_stream_events(&answer->stream, &total);
    cJSON *written = NULL;
    int same = events != NULL && ir_json_write_events(events, total, &written) == SPISOR_OK &&
               anthropic_ir_json_equals(written, expected);
    cJSON_Delete(written);
    free(events);
    return same;
}

/** The texts of `count` body answers joined, freed with free. */
static inline char *anthropic_joined_texts(const forebay_anthropic_body_answer *texts, size_t count, size_t *length) {
    *length = 0;
    for (size_t index = 0; index < count; index++) {
        *length += texts[index].length;
    }
    char *joined = malloc(*length + 1);
    size_t offset = 0;
    for (size_t index = 0; joined != NULL && index < count; index++) {
        memcpy(joined + offset, texts[index].bytes, texts[index].length);
        offset += texts[index].length;
    }
    if (joined != NULL) {
        joined[*length] = '\0';
    }
    return joined;
}

/** Whether the stream's texts, joined, are the vector's `expected` strings joined, byte for byte. */
static inline int anthropic_stream_text_matches(const anthropic_answer *answer, const cJSON *expected) {
    size_t length = 0;
    char *actual = anthropic_joined_texts(answer->stream.texts, answer->stream.count, &length);
    anthropic_input wanted = {0};
    int same = anthropic_read_stream_text(expected, &wanted) && actual != NULL && length == wanted.joined_length &&
               memcmp(actual, wanted.joined, length) == 0;
    if (!same) {
        fprintf(stderr, "  expected %s\n  actual   %s\n", wanted.joined == NULL ? "(none)" : wanted.joined,
                actual == NULL ? "(none)" : actual);
    }
    anthropic_input_release(&wanted);
    free(actual);
    return same;
}

/** The count `member` of `json` holds, 0 when it is no number. */
static inline uint64_t anthropic_count_member(const cJSON *json, const char *member) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, member);
    return cJSON_IsNumber(item) ? (uint64_t)item->valuedouble : 0;
}

/** The forebay_reply_kind a vector names, 0 for a name reply.h does not give. */
static inline uint32_t anthropic_reply_kind_named(const char *name) {
    static const char *const names[] = {"proxyError", "apiError", "providerFailure", "rateLimited", "invalidRequest"};
    for (uint32_t index = 0; name != NULL && index < sizeof names / sizeof names[0]; index++) {
        if (strcmp(names[index], name) == 0) {
            return index + 1;
        }
    }
    return 0;
}

/** A wire-shape error vector's input: the reply kind and the message, borrowed from the JSON. */
static inline int anthropic_read_error_input(const cJSON *json, anthropic_input *input) {
    *input = (anthropic_input){0};
    input->error = (forebay_anthropic_error_argument)SPISOR_INIT(forebay_anthropic_error_argument);
    input->error.reply_kind =
        anthropic_reply_kind_named(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, "replyKind")));
    input->error.message = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, "message"));
    return cJSON_IsObject(json);
}

static inline void anthropic_read_listed_model(const cJSON *json, forebay_catalog_model *model) {
    const cJSON *context = cJSON_GetObjectItemCaseSensitive(json, "context");
    const cJSON *output = cJSON_GetObjectItemCaseSensitive(json, "output");
    model->id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, "id"));
    model->name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, "name"));
    model->has_context = cJSON_IsNumber(context) ? 1 : 0;
    model->context = anthropic_count_member(json, "context");
    model->has_output = cJSON_IsNumber(output) ? 1 : 0;
    model->output = anthropic_count_member(json, "output");
}

/** A wire-shape listing vector's input: the entries, the strings borrowed from the JSON. */
static inline int anthropic_read_listing_input(const cJSON *json, anthropic_input *input) {
    *input = (anthropic_input){0};
    const cJSON *models = cJSON_GetObjectItemCaseSensitive(json, "models");
    size_t model_count = (size_t)cJSON_GetArraySize(models);
    input->models = calloc(model_count + 1, sizeof *input->models);
    if (!cJSON_IsArray(models) || input->models == NULL) {
        support_fail("a listing input", "the test could not read or allocate its entries");
        anthropic_input_release(input);
        return 0;
    }
    size_t index = 0;
    const cJSON *model = NULL;
    cJSON_ArrayForEach(model, models) {
        anthropic_read_listed_model(model, &input->models[index++]);
    }
    input->listing = (forebay_anthropic_model_listing_argument)SPISOR_INIT(forebay_anthropic_model_listing_argument);
    input->listing.models = input->models;
    input->listing.model_count = model_count;
    return 1;
}

/** A wire-shape lookup vector's input: a listing's, and the `id` to look up, borrowed from the JSON. */
static inline int anthropic_read_lookup_input(const cJSON *json, anthropic_input *input) {
    int read = anthropic_read_listing_input(json, input);
    input->lookup_id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, "id"));
    return read;
}

static inline spisor_status anthropic_call_model_lookup(const anthropic_input *input, anthropic_answer *answer,
                                                        spisor_refusal *refusal) {
    forebay_anthropic_model_lookup_argument argument = SPISOR_INIT(forebay_anthropic_model_lookup_argument);
    argument.models = input->listing.models;
    argument.model_count = input->listing.model_count;
    argument.id = input->lookup_id;
    return anthropic_definition()->model_lookup(NULL, &argument, &answer->lookup, refusal);
}

static inline int anthropic_lookup_matches(const anthropic_answer *answer, const cJSON *expected) {
    forebay_anthropic_body_answer body = SPISOR_INIT(forebay_anthropic_body_answer);
    body.bytes = answer->lookup.bytes;
    body.length = answer->lookup.length;
    return anthropic_body_equals(&body, expected);
}

static inline void anthropic_release_lookup(anthropic_answer *answer) {
    anthropic_definition()->release_model_lookup(NULL, &answer->lookup);
}

static inline spisor_status anthropic_call_error_envelope(const anthropic_input *input, anthropic_answer *answer,
                                                          spisor_refusal *refusal) {
    return anthropic_definition()->error_envelope(NULL, &input->error, &answer->body, refusal);
}

static inline spisor_status anthropic_call_model_listing(const anthropic_input *input, anthropic_answer *answer,
                                                         spisor_refusal *refusal) {
    return anthropic_definition()->model_listing(NULL, &input->listing, &answer->body, refusal);
}

/** A rate-limit vector's input: the headers in order, `resetAt` when present, `now`, the offset and both zone names. */
static inline int anthropic_read_rate_limit_input(const cJSON *json, anthropic_input *input) {
    *input = (anthropic_input){0};
    const cJSON *headers = cJSON_GetObjectItemCaseSensitive(json, "upstreamHeaders");
    const cJSON *reset_at = cJSON_GetObjectItemCaseSensitive(json, "resetAt");
    const cJSON *offset_minutes = cJSON_GetObjectItemCaseSensitive(json, "offsetMinutes");
    size_t count = (size_t)cJSON_GetArraySize(headers);
    input->pairs = calloc(count + 1, sizeof *input->pairs);
    if (!cJSON_IsObject(headers) || input->pairs == NULL) {
        support_fail("a rate-limit input", "the test could not read or allocate its headers");
        anthropic_input_release(input);
        return 0;
    }
    size_t index = 0;
    const cJSON *header = NULL;
    cJSON_ArrayForEach(header, headers) {
        input->pairs[index].name = header->string;
        input->pairs[index++].value = cJSON_GetStringValue(header);
    }
    input->rate_limit = (forebay_anthropic_rate_limit_argument)SPISOR_INIT(forebay_anthropic_rate_limit_argument);
    input->rate_limit.upstream_headers = input->pairs;
    input->rate_limit.upstream_header_count = count;
    input->rate_limit.has_reset_at = cJSON_IsNumber(reset_at) ? 1 : 0;
    input->rate_limit.reset_at = cJSON_IsNumber(reset_at) ? reset_at->valuedouble : 0;
    input->rate_limit.now = cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(json, "now"));
    input->rate_limit.offset_minutes = cJSON_IsNumber(offset_minutes) ? (int64_t)offset_minutes->valuedouble : 0;
    input->rate_limit.zone_en_us = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, "zoneEnUs"));
    input->rate_limit.zone_en_gb = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, "zoneEnGb"));
    return 1;
}

static inline spisor_status anthropic_call_rate_limit_reply(const anthropic_input *input, anthropic_answer *answer,
                                                            spisor_refusal *refusal) {
    return anthropic_definition()->rate_limit_reply(NULL, &input->rate_limit, &answer->reply, refusal);
}

/** Whether a reply has the vector's status, its headers in order and its body bytes. */
static inline int anthropic_reply_matches(const anthropic_answer *answer, const cJSON *expected) {
    const forebay_anthropic_reply_answer *reply = &answer->reply;
    const cJSON *headers = cJSON_GetObjectItemCaseSensitive(expected, "headers");
    const char *body = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(expected, "body"));
    int same = (double)reply->status == cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(expected, "status")) &&
               reply->header_count == (size_t)cJSON_GetArraySize(headers) && reply->body != NULL && body != NULL &&
               strcmp(reply->body, body) == 0;
    size_t index = 0;
    const cJSON *header = NULL;
    cJSON_ArrayForEach(header, headers) {
        same = same && index < reply->header_count && strcmp(reply->headers[index].name, header->string) == 0 &&
               strcmp(reply->headers[index].value, cJSON_GetStringValue(header)) == 0;
        index++;
    }
    if (!same) {
        fprintf(stderr, "  expected %zu headers and body %s\n  actual   status %u, %zu headers and body %s\n",
                (size_t)cJSON_GetArraySize(headers), body, (unsigned)reply->status, reply->header_count,
                reply->body);
        for (size_t shown = 0; shown < reply->header_count; shown++) {
            fprintf(stderr, "    %s: %s\n", reply->headers[shown].name, reply->headers[shown].value);
        }
    }
    return same;
}

static inline void anthropic_release_reply(anthropic_answer *answer) {
    anthropic_definition()->release_reply(NULL, &answer->reply);
}

/** The members of a JSON object as the input's pairs, in order, borrowing their names and values; 1 when read. */
static inline int anthropic_read_pairs(const cJSON *object, anthropic_input *input) {
    input->pair_count = (size_t)cJSON_GetArraySize(object);
    input->pairs = calloc(input->pair_count + 1, sizeof *input->pairs);
    if (!cJSON_IsObject(object) || input->pairs == NULL) {
        support_fail("a header list", "the test could not read or allocate its headers");
        return 0;
    }
    size_t index = 0;
    const cJSON *member = NULL;
    cJSON_ArrayForEach(member, object) {
        input->pairs[index].name = member->string;
        input->pairs[index++].value = cJSON_GetStringValue(member);
    }
    return 1;
}

/** A decode vector with headers: the body printed to bytes and the client's headers. */
static inline int anthropic_read_headed_body(const cJSON *json, anthropic_input *input) {
    *input = (anthropic_input){0};
    const char *raw = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, "rawBody"));
    size_t raw_length = raw == NULL ? 0 : strlen(raw);
    input->printed = raw != NULL ? cJSON_malloc(raw_length + 1)
                                 : cJSON_PrintUnformatted(cJSON_GetObjectItemCaseSensitive(json, "body"));
    if (raw != NULL && input->printed != NULL) {
        memcpy(input->printed, raw, raw_length + 1);
    }
    input->text = input->printed;
    if (input->printed == NULL || !anthropic_read_pairs(cJSON_GetObjectItemCaseSensitive(json, "headers"), input)) {
        anthropic_input_release(input);
        return 0;
    }
    return 1;
}

static inline spisor_status anthropic_call_decode_headed_request(const anthropic_input *input,
                                                                 anthropic_answer *answer,
                                                                 spisor_refusal *refusal) {
    forebay_anthropic_body_argument argument = SPISOR_INIT(forebay_anthropic_body_argument);
    argument.bytes = (const uint8_t *)input->text;
    argument.length = strlen(input->text);
    argument.headers = input->pairs;
    argument.header_count = input->pair_count;
    return anthropic_definition()->decode_request(NULL, &argument, &answer->request, refusal);
}

/** Whether a refused answer holds no request and the invalid-request reply of the vector. */
static inline int anthropic_invalid_request_matches(const forebay_anthropic_request_answer *request,
                                                    const cJSON *reply) {
    const char *message = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(reply, "message"));
    return request->request == NULL && request->forwarded_header_count == 0 &&
           request->reply_kind == FOREBAY_REPLY_INVALID_REQUEST &&
           (double)request->status == cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(reply, "status")) &&
           request->message != NULL && message != NULL && strcmp(request->message, message) == 0;
}

/** Whether a read answer holds the model of the vector, no reply, and the forwarded headers in order. */
static inline int anthropic_forwarding_matches(const forebay_anthropic_request_answer *request,
                                               const cJSON *expected) {
    const cJSON *forwarded = cJSON_GetObjectItemCaseSensitive(expected, "forwardedHeaders");
    const char *model = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(expected, "model"));
    int same = request->request != NULL && request->reply_kind == 0 && request->message == NULL && model != NULL &&
               strcmp(request->request->model, model) == 0 &&
               request->forwarded_header_count == (size_t)cJSON_GetArraySize(forwarded);
    size_t index = 0;
    const cJSON *header = NULL;
    cJSON_ArrayForEach(header, forwarded) {
        same = same && index < request->forwarded_header_count &&
               strcmp(request->forwarded_headers[index].name, header->string) == 0 &&
               strcmp(request->forwarded_headers[index].value, cJSON_GetStringValue(header)) == 0;
        index++;
    }
    return same;
}

static inline int anthropic_headed_request_matches(const anthropic_answer *answer, const cJSON *expected) {
    const cJSON *reply = cJSON_GetObjectItemCaseSensitive(expected, "reply");
    return reply != NULL ? anthropic_invalid_request_matches(&answer->request, reply)
                         : anthropic_forwarding_matches(&answer->request, expected);
}

/** A route vector's input: the method and the path, borrowed from the vector. */
static inline int anthropic_read_route_input(const cJSON *json, anthropic_input *input) {
    *input = (anthropic_input){0};
    input->route = (forebay_anthropic_route_argument)SPISOR_INIT(forebay_anthropic_route_argument);
    input->route.method = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, "method"));
    input->route.path = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, "path"));
    return input->route.method != NULL && input->route.path != NULL;
}

static inline spisor_status anthropic_call_route_request(const anthropic_input *input, anthropic_answer *answer,
                                                         spisor_refusal *refusal) {
    return anthropic_definition()->route_request(NULL, &input->route, &answer->route, refusal);
}

/** Whether the answer's key headers are the vector's `keyHeaders`, in order, none when it names none. */
static inline int anthropic_key_headers_match(const forebay_anthropic_route_answer *route, const cJSON *expected) {
    const cJSON *names = cJSON_GetObjectItemCaseSensitive(expected, "keyHeaders");
    size_t index = 0;
    const cJSON *name = NULL;
    if (route->key_header_count != (size_t)cJSON_GetArraySize(names) ||
        (route->key_headers == NULL) != (route->key_header_count == 0)) {
        return 0;
    }
    cJSON_ArrayForEach(name, names) {
        if (!cJSON_IsString(name) || strcmp(route->key_headers[index++], name->valuestring) != 0) {
            return 0;
        }
    }
    return 1;
}

static inline int anthropic_route_matches(const anthropic_answer *answer, const cJSON *expected) {
    static const char *const kinds[] = {"messages", "modelList", "modelLookup", "passThrough"};
    const char *kind = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(expected, "kind"));
    const char *model_id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(expected, "modelId"));
    int same = kind != NULL && answer->route.kind >= 1 && answer->route.kind <= 4 &&
               strcmp(kinds[answer->route.kind - 1], kind) == 0;
    return same && anthropic_key_headers_match(&answer->route, expected) &&
           (model_id == NULL ? answer->route.model_id == NULL
                             : answer->route.model_id != NULL && strcmp(answer->route.model_id, model_id) == 0);
}

static inline void anthropic_release_route_answer(anthropic_answer *answer) {
    anthropic_definition()->release_route(NULL, &answer->route);
}

/** A reset vector's input: the headers in order, borrowed from the vector, and `now`. */
static inline int anthropic_read_reset_input(const cJSON *json, anthropic_input *input) {
    *input = (anthropic_input){0};
    if (!anthropic_read_pairs(cJSON_GetObjectItemCaseSensitive(json, "headers"), input)) {
        anthropic_input_release(input);
        return 0;
    }
    input->reset = (forebay_anthropic_reset_argument)SPISOR_INIT(forebay_anthropic_reset_argument);
    input->reset.headers = input->pairs;
    input->reset.header_count = input->pair_count;
    input->reset.now = cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(json, "now"));
    return 1;
}

static inline spisor_status anthropic_call_reset_time(const anthropic_input *input, anthropic_answer *answer,
                                                      spisor_refusal *refusal) {
    return anthropic_definition()->reset_time(NULL, &input->reset, &answer->reset, refusal);
}

static inline int anthropic_reset_matches(const anthropic_answer *answer, const cJSON *expected) {
    const cJSON *has_reset_at = cJSON_GetObjectItemCaseSensitive(expected, "hasResetAt");
    double reset_at = cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(expected, "resetAt"));
    int same = cJSON_IsBool(has_reset_at) && answer->reset.has_reset_at == (uint32_t)cJSON_IsTrue(has_reset_at) &&
               answer->reset.reset_at == reset_at;
    if (!same) {
        fprintf(stderr, "  expected %d at %.0f\n  actual   %u at %.0f\n", cJSON_IsTrue(has_reset_at), reset_at,
                (unsigned)answer->reset.has_reset_at, answer->reset.reset_at);
    }
    return same;
}

/** A reset answer owns nothing, so there is nothing to release. */
static inline void anthropic_release_reset_answer(anthropic_answer *answer) {
    (void)answer;
}

/** The direction a vector's `direction` member names, NULL for one this service does not have. */
static inline const anthropic_direction *anthropic_direction_named(const char *name) {
    static const anthropic_direction directions[] = {
        {"decodeRequest", (uint32_t)sizeof(forebay_anthropic_request_answer), anthropic_read_wire_text,
         anthropic_call_decode_request, anthropic_request_matches, anthropic_release_request},
        {"encodeRequest", (uint32_t)sizeof(forebay_anthropic_body_answer), anthropic_read_request_input,
         anthropic_call_encode_request, anthropic_body_matches, anthropic_release_body},
        {"decodeResponse", (uint32_t)sizeof(forebay_anthropic_response_answer), anthropic_read_wire_text,
         anthropic_call_decode_response, anthropic_response_matches, anthropic_release_response},
        {"encodeResponse", (uint32_t)sizeof(forebay_anthropic_body_answer), anthropic_read_response_input,
         anthropic_call_encode_response, anthropic_body_matches, anthropic_release_body},
        {"decodeStream", (uint32_t)sizeof(anthropic_stream_answer), anthropic_read_stream_text,
         anthropic_call_decode_stream, anthropic_stream_events_match, anthropic_release_stream_answers},
        {"encodeStream", (uint32_t)sizeof(anthropic_stream_answer), anthropic_read_events_input,
         anthropic_call_encode_stream, anthropic_stream_text_matches, anthropic_release_stream_answers},
        {"errorEnvelope", (uint32_t)sizeof(forebay_anthropic_body_answer), anthropic_read_error_input,
         anthropic_call_error_envelope, anthropic_body_matches, anthropic_release_body},
        {"modelListing", (uint32_t)sizeof(forebay_anthropic_body_answer), anthropic_read_listing_input,
         anthropic_call_model_listing, anthropic_body_matches, anthropic_release_body},
        {"modelLookup", (uint32_t)sizeof(forebay_anthropic_model_lookup_answer), anthropic_read_lookup_input,
         anthropic_call_model_lookup, anthropic_lookup_matches, anthropic_release_lookup},
        {"rateLimitReply", (uint32_t)sizeof(forebay_anthropic_reply_answer), anthropic_read_rate_limit_input,
         anthropic_call_rate_limit_reply, anthropic_reply_matches, anthropic_release_reply},
        {"decodeRequestWithHeaders", (uint32_t)sizeof(forebay_anthropic_request_answer), anthropic_read_headed_body,
         anthropic_call_decode_headed_request, anthropic_headed_request_matches, anthropic_release_request},
        {"routeRequest", (uint32_t)sizeof(forebay_anthropic_route_answer), anthropic_read_route_input,
         anthropic_call_route_request, anthropic_route_matches, anthropic_release_route_answer},
        {"resetTime", (uint32_t)sizeof(forebay_anthropic_reset_answer), anthropic_read_reset_input,
         anthropic_call_reset_time, anthropic_reset_matches, anthropic_release_reset_answer},
    };
    for (size_t index = 0; name != NULL && index < sizeof directions / sizeof directions[0]; index++) {
        if (strcmp(directions[index].name, name) == 0) {
            return &directions[index];
        }
    }
    return NULL;
}

/** The direction of a vector, NULL for one this service does not have. */
static inline const anthropic_direction *anthropic_direction_of(const cJSON *vector) {
    return anthropic_direction_named(
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(vector, "direction")));
}

/** Whether the vector's call allocates, which a reset never does, nor a route to anything but a model lookup. */
static inline int anthropic_vector_allocates(const cJSON *vector) {
    const char *name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(vector, "direction"));
    const cJSON *output = cJSON_GetObjectItemCaseSensitive(vector, "output");
    if (name != NULL && strcmp(name, "resetTime") == 0) {
        return 0;
    }
    return name == NULL || strcmp(name, "routeRequest") != 0 ||
           cJSON_GetObjectItemCaseSensitive(output, "modelId") != NULL;
}

/** Calls the direction on the input and requires OK with an answer that matches `expected`. */
static inline void anthropic_check_direction(const anthropic_direction *direction, const char *label,
                                             const anthropic_input *input, const cJSON *expected) {
    anthropic_answer answer;
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    anthropic_start_answer(direction, &answer);
    spisor_status status = direction->call(input, &answer, &refusal);
    if (status != SPISOR_OK) {
        support_failf(label, "%s answered %d", direction->name, (int)status);
    } else if (!direction->matches(&answer, expected)) {
        support_failf(label, "%s did not answer the expected one", direction->name);
    }
    direction->release(&answer);
    anthropic_definition()->release_refusal(NULL, &refusal);
}

/** Reads the vector input as the direction does, then checks the direction on it. */
static inline void anthropic_check_vector_input(const anthropic_direction *direction, const char *label,
                                                const cJSON *input_json, const cJSON *expected) {
    anthropic_input input;
    if (!direction->read_input(input_json, &input)) {
        support_fail(label, "the input is not one the test reads");
    } else {
        anthropic_check_direction(direction, label, &input, expected);
    }
    anthropic_input_release(&input);
}

#endif
