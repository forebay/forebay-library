#include <string.h>

#include "anthropic_byte_buffer.h"
#include "anthropic_encode.h"
#include "anthropic_operations.h"
#include "definition_support.h"
#include "spisor/allocation.h"
#include "spisor/json_write.h"

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

spisor_status anthropic_stream_encoder_open(void *context, forebay_anthropic_stream_encoder **encoder,
                                            spisor_refusal *refusal) {
    (void)context;
    if (encoder == NULL || *encoder != NULL) {
        return spisor_refuse_shape(refusal, "anthropic's stream_encoder_open");
    }
    forebay_anthropic_stream_encoder *opened = spisor_memory_allocate_zeroed(1, sizeof *opened);
    if (opened == NULL) {
        return SPISOR_OUT_OF_MEMORY;
    }
    *encoder = opened;
    return SPISOR_OK;
}

static int append_text(anthropic_byte_buffer *text, const char *bytes) {
    return anthropic_byte_buffer_append(text, bytes, strlen(bytes));
}

/** Appends one event's frame: `event:` naming the data's type, `data:` holding its JSON, and a blank line. */
static int append_frame(anthropic_byte_buffer *text, const cJSON *data) {
    char *json = spisor_json_stringify(data);
    int appended = json != NULL && append_text(text, "event: ") &&
                   append_text(text, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(data, "type"))) &&
                   append_text(text, "\ndata: ") && append_text(text, json) && append_text(text, "\n\n");
    spisor_memory_release(json);
    return appended;
}

/** Writes every event's frame into `text`; OK, MALFORMED with the writer's problem, or OUT_OF_MEMORY. */
static spisor_status write_frames(const forebay_anthropic_stream_encode_argument *argument,
                                  anthropic_byte_buffer *text, anthropic_problem *problem) {
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
static int fill_body(const anthropic_byte_buffer *text, forebay_anthropic_body_answer *answer) {
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
    anthropic_byte_buffer text = {NULL, 0, 0};
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
