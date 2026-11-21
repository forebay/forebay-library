#include <string.h>

#include "anthropic_decode.h"
#include "anthropic_frames.h"
#include "anthropic_operations.h"
#include "definition_support.h"
#include "spisor/allocation.h"
#include "spisor/json_kind.h"

#define DECODE_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_stream_decode_argument, length)
#define EVENTS_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_events_answer, storage)

spisor_status anthropic_stream_decoder_open(void *context, forebay_anthropic_stream_decoder **decoder,
                                            spisor_refusal *refusal) {
    (void)context;
    if (decoder == NULL || *decoder != NULL) {
        return spisor_refuse_shape(refusal, "anthropic's stream_decoder_open");
    }
    forebay_anthropic_stream_decoder *opened = spisor_memory_allocate_zeroed(1, sizeof *opened);
    if (opened == NULL) {
        return SPISOR_OUT_OF_MEMORY;
    }
    *decoder = opened;
    return SPISOR_OK;
}

/** How many frames the scan will complete, read from a copy, so the scan itself is left where it was. */
static size_t frame_count(const anthropic_frame_scan *scan) {
    anthropic_frame_scan copy = *scan;
    anthropic_frame frame;
    size_t count = 0;
    while (anthropic_frames_next(&copy, &frame)) {
        count++;
    }
    return count;
}

/**
 * The event of one frame into `*event`, 1 when it yields one.
 *
 * @implNote A frame cut short by a dropped connection is not JSON and yields nothing, as a frame of an unknown type
 * does; so does JSON with a NUL in a string, which this library cannot read.
 */
static int decode_frame(anthropic_reader *reader, const anthropic_frame_scan *scan, const anthropic_frame *frame,
                        forebay_ir_event *event) {
    size_t length = 0;
    int out_of_memory = 0;
    char *payload = anthropic_frame_payload(scan, frame, &length, &out_of_memory);
    cJSON *root =
        payload == NULL ? NULL : spisor_parse_json_reporting((const uint8_t *)payload, length, &out_of_memory);
    spisor_memory_release(payload);
    int decoded = root != NULL && anthropic_decode_stream_frame(reader, root, event);
    cJSON_Delete(root);
    reader->out_of_memory |= out_of_memory;
    return decoded;
}

/** The events of every frame the scan completes, into the reader's storage; 0 once memory has run out. */
static int decode_frames(anthropic_reader *reader, anthropic_frame_scan *scan,
                         forebay_anthropic_events_answer *answer) {
    forebay_ir_event *events = anthropic_take(reader, frame_count(scan), sizeof *events);
    size_t count = 0;
    anthropic_frame frame;
    while (!reader->out_of_memory && anthropic_frames_next(scan, &frame)) {
        if (decode_frame(reader, scan, &frame, &events[count])) {
            count++;
        } else {
            memset(&events[count], 0, sizeof events[count]);
        }
    }
    answer->events = count == 0 ? NULL : events;
    answer->event_count = count;
    return !reader->out_of_memory;
}

/** Feeds a piece, then the end of the stream when `ends` is 1; the decoder changes only when the answer is OK. */
static spisor_status decode_piece(const forebay_anthropic_stream_decode_argument *argument, int ends,
                                  forebay_anthropic_events_answer *answer, spisor_refusal *refusal,
                                  const char *operation) {
    if (argument == NULL || !spisor_has_shape(argument->size, DECODE_SHAPE) ||
        !SPISOR_IS_EMPTY_COUNTED_ANSWER(answer, EVENTS_ANSWER_SHAPE, answer->event_count)) {
        return spisor_refuse_shape(refusal, operation);
    }
    if (argument->decoder == NULL) {
        return forebay_refuse_member(refusal, "decoder", "a decoder stream_decoder_open answered");
    }
    if (argument->bytes == NULL && argument->length > 0) {
        return anthropic_refuse_bytes(refusal);
    }
    anthropic_frame_scan scan;
    if (!anthropic_frames_begin(argument->decoder, argument->bytes, argument->length, ends, &scan)) {
        return SPISOR_OUT_OF_MEMORY;
    }
    void *storage = NULL;
    anthropic_reader reader = {&storage, 0};
    forebay_anthropic_events_answer decoded = {0};
    if (!decode_frames(&reader, &scan, &decoded)) {
        forebay_storage_release(storage);
        return SPISOR_OUT_OF_MEMORY;
    }
    anthropic_frames_commit(argument->decoder, &scan);
    answer->events = decoded.events;
    answer->event_count = decoded.event_count;
    answer->storage = storage;
    return SPISOR_OK;
}

spisor_status anthropic_stream_decode(void *context, const forebay_anthropic_stream_decode_argument *argument,
                                      forebay_anthropic_events_answer *answer, spisor_refusal *refusal) {
    (void)context;
    return decode_piece(argument, 0, answer, refusal, "anthropic's stream_decode");
}

spisor_status anthropic_stream_decode_finish(void *context, const forebay_anthropic_stream_decode_argument *argument,
                                             forebay_anthropic_events_answer *answer, spisor_refusal *refusal) {
    (void)context;
    return decode_piece(argument, 1, answer, refusal, "anthropic's stream_decode_finish");
}

ANTHROPIC_RELEASE_ANSWER(anthropic_release_events, forebay_anthropic_events_answer, EVENTS_ANSWER_SHAPE)

void anthropic_release_stream_decoder(void *context, forebay_anthropic_stream_decoder *decoder) {
    (void)context;
    if (decoder != NULL) {
        spisor_memory_release(decoder->pending.bytes);
        spisor_memory_release(decoder);
    }
}
