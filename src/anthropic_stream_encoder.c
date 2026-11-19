#include <string.h>

#include "anthropic_encode.h"
#include "anthropic_operations.h"
#include "anthropic_stringify.h"
#include "definition_support.h"
#include "spisor/allocation.h"

#define ENCODER_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_stream_encoder_answer, encoder)
#define ENCODE_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_stream_encode_argument, event_count)

/**
 * A stream being written.
 *
 * @implNote The translator's encoder keeps nothing between events, so neither does this one, and `reserved` is there
 * only because C has no empty struct. The handle is what a stream's writing is, so a wire format that does keep
 * state needs no other calls.
 */
struct forebay_anthropic_stream_encoder {
    uint32_t reserved;
};

/** SSE text being built, from the binary's one allocator. */
typedef struct stream_text {
    char *bytes;
    size_t length;
    size_t capacity;
} stream_text;

spisor_status anthropic_stream_encoder_open(void *context, forebay_anthropic_stream_encoder_answer *answer,
                                            spisor_refusal *refusal) {
    (void)context;
    if (answer == NULL || !spisor_has_shape(answer->size, ENCODER_ANSWER_SHAPE) || answer->encoder != NULL) {
        return spisor_refuse_shape(refusal, "anthropic's stream_encoder_open");
    }
    forebay_anthropic_stream_encoder *encoder = spisor_memory_allocate_zeroed(1, sizeof *encoder);
    if (encoder == NULL) {
        return SPISOR_OUT_OF_MEMORY;
    }
    answer->encoder = encoder;
    return SPISOR_OK;
}

/** Appends `length` bytes; 0 when memory runs out, the text kept as it was. */
static int append(stream_text *text, const char *bytes, size_t length) {
    if (length > SIZE_MAX - 1 - text->length) {
        return 0;
    }
    if (text->length + length + 1 > text->capacity) {
        size_t capacity = text->capacity > (SIZE_MAX - 1) / 2 ? SIZE_MAX : text->capacity * 2;
        capacity = capacity > text->length + length + 1 ? capacity : text->length + length + 1;
        char *grown = spisor_memory_reallocate(text->bytes, capacity);
        if (grown == NULL) {
            return 0;
        }
        text->bytes = grown;
        text->capacity = capacity;
    }
    memcpy(text->bytes + text->length, bytes, length);
    text->length += length;
    text->bytes[text->length] = '\0';
    return 1;
}

static int append_text(stream_text *text, const char *bytes) {
    return append(text, bytes, strlen(bytes));
}

/** Appends one event's frame: `event:` naming the data's type, `data:` holding its JSON, and a blank line. */
static int append_frame(stream_text *text, const cJSON *data) {
    size_t length = 0;
    char *json = anthropic_stringify(data, &length);
    int appended = json != NULL && append_text(text, "event: ") &&
                   append_text(text, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(data, "type"))) &&
                   append_text(text, "\ndata: ") && append(text, json, length) && append_text(text, "\n\n");
    spisor_memory_release(json);
    return appended;
}

/** Writes every event's frame into `text`; OK, MALFORMED with the writer's problem, or OUT_OF_MEMORY. */
static spisor_status write_frames(const forebay_anthropic_stream_encode_argument *argument, stream_text *text,
                                  anthropic_problem *problem) {
    for (size_t index = 0; index < argument->event_count; index++) {
        anthropic_writer writer = {SPISOR_OK, NULL};
        cJSON *data = anthropic_encode_stream_event(&writer, &argument->events[index]);
        int appended = writer.status == SPISOR_OK && append_frame(text, data);
        cJSON_Delete(data);
        if (writer.status == SPISOR_MALFORMED) {
            anthropic_writer_problem(&writer, problem);
            return SPISOR_MALFORMED;
        }
        if (!appended) {
            return SPISOR_OUT_OF_MEMORY;
        }
    }
    return SPISOR_OK;
}

/** Fills the answer with a copy of the text, "" for none; 0 when memory runs out, the answer left empty. */
static int fill_body(const stream_text *text, forebay_anthropic_body_answer *answer) {
    void *storage = NULL;
    char *body = forebay_storage_allocate(&storage, text->length + 1);
    if (body == NULL) {
        return 0;
    }
    if (text->length > 0) {
        memcpy(body, text->bytes, text->length);
    }
    answer->bytes = (const uint8_t *)body;
    answer->length = text->length;
    answer->storage = storage;
    return 1;
}

static spisor_status encode_events(const forebay_anthropic_stream_encode_argument *argument,
                                   forebay_anthropic_body_answer *answer, spisor_refusal *refusal,
                                   const char *operation) {
    if (argument == NULL || !spisor_has_shape(argument->size, ENCODE_SHAPE) ||
        !SPISOR_IS_EMPTY_ANSWER(answer, ANTHROPIC_BODY_ANSWER_SHAPE)) {
        return spisor_refuse_shape(refusal, operation);
    }
    if (argument->encoder == NULL) {
        return forebay_refuse_member(refusal, "encoder", "an encoder stream_encoder_open answered");
    }
    anthropic_problem problem;
    spisor_status status = anthropic_check_events(argument->events, argument->event_count, &problem);
    stream_text text = {NULL, 0, 0};
    if (status == SPISOR_OK) {
        status = write_frames(argument, &text, &problem);
    }
    if (status == SPISOR_OK && !fill_body(&text, answer)) {
        status = SPISOR_OUT_OF_MEMORY;
    }
    spisor_memory_release(text.bytes);
    return status == SPISOR_MALFORMED ? anthropic_refuse_problem(refusal, &problem) : status;
}

spisor_status anthropic_stream_encode(void *context, const forebay_anthropic_stream_encode_argument *argument,
                                      forebay_anthropic_body_answer *answer, spisor_refusal *refusal) {
    (void)context;
    return encode_events(argument, answer, refusal, "anthropic's stream_encode");
}

spisor_status anthropic_stream_encode_finish(void *context, const forebay_anthropic_stream_encode_argument *argument,
                                             forebay_anthropic_body_answer *answer, spisor_refusal *refusal) {
    (void)context;
    return encode_events(argument, answer, refusal, "anthropic's stream_encode_finish");
}

void anthropic_release_stream_encoder(void *context, forebay_anthropic_stream_encoder *encoder) {
    (void)context;
    spisor_memory_release(encoder);
}
