#include "anthropic_call.h"

#include "spisor/json_allocation.h"

/**
 * @file
 * The anthropic service's streams through its definition: every `stream-*` vector in its direction, each decode
 * vector's text also fed split in two at every byte and fed one byte at a time, then the cases no vector writes: the
 * translator's own stream edge tests, line ends, a frame of no JSON, a decoder kept apart per stream and reused after
 * its finish, and the refusals of both handles.
 */

#define STREAM_VECTOR_COUNT 7

static cJSON *parse(const char *text) {
    int out_of_memory = 0;
    return spisor_parse_json_reporting((const uint8_t *)text, strlen(text), &out_of_memory);
}

static const anthropic_direction *decode_stream(void) {
    return anthropic_direction_named("decodeStream");
}

/** An input of `text` whole, copied, fed as one piece. */
static int input_of_text(const char *text, anthropic_input *input) {
    *input = (anthropic_input){0};
    size_t length = strlen(text);
    input->joined = malloc(length + 1);
    if (input->joined == NULL) {
        return 0;
    }
    memcpy(input->joined, text, length + 1);
    input->joined_length = length;
    return anthropic_input_cut(input, &length, 1);
}

/** The text split in two at every byte, the first piece empty to the second empty, each decoding as the vector says. */
static void every_split_in_two(const char *path, anthropic_input *input, const cJSON *expected) {
    for (size_t offset = 0; offset <= input->joined_length; offset++) {
        size_t lengths[2] = {offset, input->joined_length - offset};
        char label[1100];
        snprintf(label, sizeof label, "%s split at %llu", path, (unsigned long long)offset);
        if (!anthropic_input_cut(input, lengths, 2)) {
            support_fail(label, "the pieces could not be made");
            return;
        }
        anthropic_check_direction(decode_stream(), label, input, expected);
    }
}

static void one_byte_at_a_time(const char *path, anthropic_input *input, const cJSON *expected) {
    size_t *lengths = calloc(input->joined_length + 1, sizeof *lengths);
    for (size_t index = 0; lengths != NULL && index < input->joined_length; index++) {
        lengths[index] = 1;
    }
    char label[1100];
    snprintf(label, sizeof label, "%s one byte at a time", path);
    if (lengths == NULL || !anthropic_input_cut(input, lengths, input->joined_length)) {
        support_fail(label, "the pieces could not be made");
    } else {
        anthropic_check_direction(decode_stream(), label, input, expected);
    }
    free(lengths);
}

static void run_vector(const char *path) {
    cJSON *vector = spisor_vector_read(path);
    const cJSON *input_json = cJSON_GetObjectItemCaseSensitive(vector, "input");
    const cJSON *output = cJSON_GetObjectItemCaseSensitive(vector, "output");
    const anthropic_direction *direction = anthropic_direction_of(vector);
    anthropic_input input = {0};
    if (vector == NULL || input_json == NULL || output == NULL || direction == NULL) {
        support_fail(path, "not a vector with an input, an output and a direction this service has");
    } else if (!direction->read_input(input_json, &input)) {
        support_fail(path, "the input is not one the test reads");
    } else {
        anthropic_check_direction(direction, path, &input, output);
        if (direction == decode_stream()) {
            every_split_in_two(path, &input, output);
            one_byte_at_a_time(path, &input, output);
        }
    }
    anthropic_input_release(&input);
    cJSON_Delete(vector);
}

static int run_vectors(void) {
    char *list = support_read_text(ANTHROPIC_VECTOR_LIST);
    int count = 0;
    if (list == NULL) {
        support_fail("vector list", ANTHROPIC_VECTOR_LIST);
        return 0;
    }
    for (char *line = strtok(list, "\r\n"); line != NULL; line = strtok(NULL, "\r\n")) {
        if (strstr(line, "stream-") != NULL) {
            run_vector(line);
            count++;
        }
    }
    free(list);
    return count;
}

typedef struct stream_case {
    const char *label;
    const char *text;
    const char *expected;
} stream_case;

#define STOP_FRAME "data: {\"type\":\"content_block_stop\",\"index\":0}"
#define STOP_EVENT "{\"event\":\"content_block_stop\",\"index\":0}"

static const stream_case DECODE_CASES[] = {
    {"a last frame that has no terminating blank line", STOP_FRAME "\n", "[" STOP_EVENT "]"},
    {"a last line that has no newline at all", STOP_FRAME, "[" STOP_EVENT "]"},
    {"a last line ending in CR alone", STOP_FRAME "\r", "[" STOP_EVENT "]"},
    {"carries on past a data line that is not JSON",
     STOP_FRAME "\n\ndata: {\"type\":\"mess\n\n" STOP_FRAME "\n\n", "[" STOP_EVENT "," STOP_EVENT "]"},
    {"a CR alone ends no line, so a frame split by one is one line", STOP_FRAME "\r" STOP_FRAME "\n\n", "[]"},
    {"CRLF ends a line, and only one CR before LF is dropped, so CR CR LF is no blank line",
     STOP_FRAME "\r\n\r\n" STOP_FRAME "\n\r\r\n" STOP_FRAME "\n\n", "[" STOP_EVENT "]"},
    {"data lines join with LF, the space after the field optional",
     "data:{\"type\":\"content_block_stop\",\ndata:  \"index\":1}\n\n",
     "[{\"event\":\"content_block_stop\",\"index\":1}]"},
    {"comments, other fields and empty data yield nothing",
     ": hi\nid: 3\nretry: 5\nevent: x\ndata:\n\ndata: \n\n", "[]"},
    {"a JSON string holding NUL yields nothing", "data: {\"type\":\"error\",\"error\":{\"type\":\"a\\u0000\"}}\n\n",
     "[]"},
    {"an index that is no count reads as 0",
     "data: {\"type\":\"content_block_stop\",\"index\":-3}\n\ndata: {\"type\":\"content_block_stop\",\"index\":2.7}\n\n"
     "data: {\"type\":\"content_block_stop\",\"index\":\"1\"}\n\n",
     "[" STOP_EVENT ",{\"event\":\"content_block_stop\",\"index\":2}," STOP_EVENT "]"},
    {"a delta's type decides its event, an unknown one yields nothing",
     "data: {\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":\"text_delta\",\"text\":5}}\n\n"
     "data: {\"type\":\"content_block_delta\",\"index\":1,\"delta\":\"x\"}\n\n"
     "data: {\"type\":\"content_block_delta\",\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\"}}\n\n",
     "[{\"event\":\"text_delta\",\"index\":1},{\"event\":\"tool_input_delta\",\"index\":0,\"partialJson\":\"{\"}]"},
    {"a message delta keeps a wire key named like the translator's",
     "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\",\"$stopSequenceRaw\":1,"
     "\"stop_sequence\":\"s\"},\"usage\":[]}\n\n",
     "[{\"event\":\"message_delta\",\"stopReason\":\"tool_use\",\"extensions\":{\"$stopSequenceRaw\":1}}]"},
    {"a message start without a message object yields nothing, one with no members an empty start",
     "data: {\"type\":\"message_start\",\"message\":[]}\n\ndata: {\"type\":\"message_start\",\"message\":{}}\n\n",
     "[{\"event\":\"message_start\"}]"},
};

static void run_decode_cases(void) {
    for (size_t index = 0; index < sizeof DECODE_CASES / sizeof DECODE_CASES[0]; index++) {
        cJSON *expected = parse(DECODE_CASES[index].expected);
        anthropic_input input = {0};
        if (expected == NULL || !input_of_text(DECODE_CASES[index].text, &input)) {
            support_fail(DECODE_CASES[index].label, "the case could not be read");
        } else {
            anthropic_check_direction(decode_stream(), DECODE_CASES[index].label, &input, expected);
            every_split_in_two(DECODE_CASES[index].label, &input, expected);
        }
        anthropic_input_release(&input);
        cJSON_Delete(expected);
    }
}

/** Feeds `text` to `decoder` and requires OK and `count` events. */
static void expect_events(const char *label, forebay_anthropic_stream_decoder *decoder, const char *text, int finish,
                          size_t count) {
    anthropic_piece piece = {text, strlen(text)};
    forebay_anthropic_events_answer answer;
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    spisor_status status = anthropic_feed_decoder(decoder, &piece, finish, &answer, &refusal);
    if (status != SPISOR_OK || answer.event_count != count) {
        support_failf(label, "answered %d with %llu events, expected %llu", (int)status,
                      (unsigned long long)answer.event_count, (unsigned long long)count);
    }
    anthropic_definition()->release_events(NULL, &answer);
    anthropic_definition()->release_refusal(NULL, &refusal);
}

static forebay_anthropic_stream_decoder *open_decoder(void) {
    forebay_anthropic_stream_decoder_answer opened = SPISOR_INIT(forebay_anthropic_stream_decoder_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    if (anthropic_definition()->stream_decoder_open(NULL, &opened, &refusal) != SPISOR_OK) {
        support_fail("open a decoder", "did not answer OK");
    }
    anthropic_definition()->release_refusal(NULL, &refusal);
    return opened.decoder;
}

/** Two decoders keep their frames apart, and a finished decoder reads a new stream from nothing. */
static void a_decoder_keeps_its_own_stream_and_starts_anew_after_finishing(void) {
    forebay_anthropic_stream_decoder *first = open_decoder();
    forebay_anthropic_stream_decoder *second = open_decoder();
    expect_events("first, half a frame", first, "data: {\"type\":\"message_", 0, 0);
    expect_events("second, a frame of its own", second, STOP_FRAME "\n\n", 0, 1);
    expect_events("first, the rest of its frame", first, "stop\"}\n\n", 0, 1);
    expect_events("second, half a frame then the end", second, "data: {\"type\":\"message_", 1, 0);
    expect_events("second, a new stream after its finish", second, "stop\"}\n\n", 1, 0);
    expect_events("second, a whole frame after its finish", second, STOP_FRAME, 1, 1);
    expect_events("first, a last piece given to the finish", first, STOP_FRAME, 1, 1);
    anthropic_definition()->release_stream_decoder(NULL, first);
    anthropic_definition()->release_stream_decoder(NULL, second);
    anthropic_definition()->release_stream_decoder(NULL, NULL);
    anthropic_definition()->release_stream_encoder(NULL, NULL);
}

static void expect_malformed_decode(const char *label, const forebay_anthropic_stream_decode_argument *argument) {
    forebay_anthropic_events_answer answer = SPISOR_INIT(forebay_anthropic_events_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    spisor_status status = anthropic_definition()->stream_decode(NULL, argument, &answer, &refusal);
    if (status != SPISOR_MALFORMED || refusal.detail == NULL || answer.storage != NULL || answer.event_count != 0) {
        support_failf(label, "answered %d, not MALFORMED with a refusal and an empty answer", (int)status);
    }
    anthropic_definition()->release_refusal(NULL, &refusal);
}

static void a_decode_without_a_decoder_or_its_bytes_is_malformed(void) {
    forebay_anthropic_stream_decode_argument argument = SPISOR_INIT(forebay_anthropic_stream_decode_argument);
    expect_malformed_decode("a NULL decoder", &argument);
    argument.decoder = open_decoder();
    argument.length = 3;
    expect_malformed_decode("NULL bytes beside a length", &argument);
    forebay_anthropic_stream_decoder_answer opened = SPISOR_INIT(forebay_anthropic_stream_decoder_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    opened.decoder = argument.decoder;
    if (anthropic_definition()->stream_decoder_open(NULL, &opened, &refusal) != SPISOR_MALFORMED) {
        support_fail("open into an answer holding a decoder", "was not MALFORMED");
    }
    anthropic_definition()->release_refusal(NULL, &refusal);
    anthropic_definition()->release_stream_decoder(NULL, argument.decoder);
}

static void expect_malformed_encode(const char *label, const forebay_ir_event *events, size_t count,
                                    int with_encoder) {
    forebay_anthropic_stream_encoder_answer opened = SPISOR_INIT(forebay_anthropic_stream_encoder_answer);
    forebay_anthropic_stream_encode_argument argument = SPISOR_INIT(forebay_anthropic_stream_encode_argument);
    forebay_anthropic_body_answer answer = SPISOR_INIT(forebay_anthropic_body_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    if (with_encoder && anthropic_definition()->stream_encoder_open(NULL, &opened, &refusal) != SPISOR_OK) {
        support_fail(label, "the encoder did not open");
    }
    argument.encoder = opened.encoder;
    argument.events = events;
    argument.event_count = count;
    spisor_status status = anthropic_definition()->stream_encode(NULL, &argument, &answer, &refusal);
    if (status != SPISOR_MALFORMED || refusal.detail == NULL || refusal.fix == NULL || answer.storage != NULL ||
        answer.bytes != NULL) {
        support_failf(label, "answered %d, not MALFORMED with a refusal and an empty answer", (int)status);
    }
    anthropic_definition()->release_refusal(NULL, &refusal);
    anthropic_definition()->release_stream_encoder(NULL, opened.encoder);
}

static void events_that_break_their_header_are_malformed(void) {
    static const spisor_value empty_text = {SPISOR_VALUE_TEXT, 0, 0, NULL, 0, NULL, NULL, 0};
    static const forebay_ir_event stop = {.kind = FOREBAY_IR_EVENT_MESSAGE_STOP};
    static const forebay_ir_event unnamed_kind = {.kind = 99};
    static const forebay_ir_event no_kind = {.kind = 0};
    static const forebay_ir_event unknown_block = {.kind = FOREBAY_IR_EVENT_CONTENT_BLOCK_START,
                                                   .block_kind = FOREBAY_IR_BLOCK_IMAGE};
    static const forebay_ir_event bad_extensions = {.kind = FOREBAY_IR_EVENT_MESSAGE_DELTA, .extensions = &empty_text};
    const forebay_ir_event after_a_good_one[] = {stop, unnamed_kind};
    expect_malformed_encode("a NULL encoder", &stop, 1, 0);
    expect_malformed_encode("events NULL beside a count", NULL, 1, 1);
    expect_malformed_encode("an event kind the header does not name", &unnamed_kind, 1, 1);
    expect_malformed_encode("an event of no kind", &no_kind, 1, 1);
    expect_malformed_encode("a block start of a block kind no start has", &unknown_block, 1, 1);
    expect_malformed_encode("extensions text without bytes", &bad_extensions, 1, 1);
    expect_malformed_encode("a bad event after a good one", after_a_good_one, 2, 1);
}

/** An encode of no events answers empty text, and a finish of no events is how a stream ends. */
static void an_encode_of_no_events_answers_empty_text(void) {
    forebay_anthropic_stream_encoder_answer opened = SPISOR_INIT(forebay_anthropic_stream_encoder_answer);
    forebay_anthropic_body_answer answer;
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    anthropic_definition()->stream_encoder_open(NULL, &opened, &refusal);
    spisor_status status = anthropic_feed_encoder(opened.encoder, NULL, 0, 1, &answer, &refusal);
    if (status != SPISOR_OK || answer.length != 0 || answer.bytes == NULL || answer.bytes[0] != '\0') {
        support_failf("an encode of no events", "answered %d with %llu bytes", (int)status,
                      (unsigned long long)answer.length);
    }
    anthropic_definition()->release_body(NULL, &answer);
    anthropic_definition()->release_refusal(NULL, &refusal);
    anthropic_definition()->release_stream_encoder(NULL, opened.encoder);
}

static void releasing_an_answer_never_filled_does_nothing(void) {
    forebay_anthropic_events_answer answer = {0};
    anthropic_definition()->release_events(NULL, &answer);
    anthropic_definition()->release_events(NULL, NULL);
}

int main(void) {
    spisor_json_use_library_allocator();
    int vectors = run_vectors();
    if (vectors != STREAM_VECTOR_COUNT) {
        support_failf("stream vectors", "%d files, expected %d", vectors, STREAM_VECTOR_COUNT);
    }
    run_decode_cases();
    a_decoder_keeps_its_own_stream_and_starts_anew_after_finishing();
    a_decode_without_a_decoder_or_its_bytes_is_malformed();
    events_that_break_their_header_are_malformed();
    an_encode_of_no_events_answers_empty_text();
    releasing_an_answer_never_filled_does_nothing();
    printf("anthropic_stream_test: %d stream vectors\n", vectors);
    return support_summary("anthropic_stream_test");
}
