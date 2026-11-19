#include "anthropic_frames.h"

#include <string.h>

#include "spisor/allocation.h"

static const char DATA_FIELD[] = "data:";
static const uint8_t STREAM_END[] = {'\n', '\n'};

/** Room for `needed` bytes, the pending text kept; 0 when memory runs out, the decoder unchanged. */
static int reserve(forebay_anthropic_stream_decoder *decoder, size_t needed) {
    if (needed <= decoder->capacity) {
        return 1;
    }
    size_t grown = decoder->capacity > SIZE_MAX / 2 ? needed : decoder->capacity * 2;
    size_t capacity = grown > needed ? grown : needed;
    uint8_t *pending = spisor_memory_reallocate(decoder->pending, capacity);
    if (pending == NULL) {
        return 0;
    }
    decoder->pending = pending;
    decoder->capacity = capacity;
    return 1;
}

int anthropic_frames_begin(forebay_anthropic_stream_decoder *decoder, const uint8_t *bytes, size_t length, int ends,
                           anthropic_frame_scan *scan) {
    size_t ending = ends ? sizeof STREAM_END : 0;
    if (length > SIZE_MAX - ending - decoder->length || !reserve(decoder, decoder->length + length + ending)) {
        return 0;
    }
    if (length > 0) {
        memcpy(decoder->pending + decoder->length, bytes, length);
    }
    if (ending > 0) {
        memcpy(decoder->pending + decoder->length + length, STREAM_END, ending);
    }
    scan->text = decoder->pending;
    scan->end = decoder->length + length + ending;
    scan->frame_start = 0;
    scan->line_start = decoder->line_start;
    scan->search_from = decoder->length;
    return 1;
}

/** Where the line from `line_start` ends, the LF at `newline`, without one CR before it. */
static size_t line_end_of(const uint8_t *text, size_t line_start, size_t newline) {
    return newline > line_start && text[newline - 1] == '\r' ? newline - 1 : newline;
}

int anthropic_frames_next(anthropic_frame_scan *scan, anthropic_frame *frame) {
    while (scan->search_from < scan->end) {
        const uint8_t *found = memchr(scan->text + scan->search_from, '\n', scan->end - scan->search_from);
        if (found == NULL) {
            break;
        }
        size_t newline = (size_t)(found - scan->text);
        size_t line_start = scan->line_start;
        scan->line_start = newline + 1;
        scan->search_from = newline + 1;
        if (line_end_of(scan->text, line_start, newline) == line_start) {
            frame->start = scan->frame_start;
            frame->end = line_start;
            scan->frame_start = newline + 1;
            return 1;
        }
    }
    scan->search_from = scan->end;
    return 0;
}

/** The data of one line from `start` to `end`, without the field name and one space; 0 when it is no data line. */
static int data_of_line(const uint8_t *text, size_t start, size_t end, size_t *data_start) {
    size_t field = sizeof DATA_FIELD - 1;
    if (end - start < field || memcmp(text + start, DATA_FIELD, field) != 0) {
        return 0;
    }
    *data_start = start + field < end && text[start + field] == ' ' ? start + field + 1 : start + field;
    return 1;
}

/**
 * Walks the frame's data lines, copying them joined into `payload` when it is not NULL.
 *
 * @return how many data lines the frame has; `*length` is their joined length.
 */
static size_t join_data_lines(const anthropic_frame_scan *scan, const anthropic_frame *frame, char *payload,
                              size_t *length) {
    size_t lines = 0;
    *length = 0;
    for (size_t start = frame->start; start < frame->end;) {
        size_t newline = (size_t)((const uint8_t *)memchr(scan->text + start, '\n', frame->end - start) - scan->text);
        size_t end = line_end_of(scan->text, start, newline);
        size_t data_start = 0;
        if (data_of_line(scan->text, start, end, &data_start)) {
            size_t separator = lines > 0 ? 1 : 0;
            if (payload != NULL) {
                memset(payload + *length, '\n', separator);
                memcpy(payload + *length + separator, scan->text + data_start, end - data_start);
            }
            *length += separator + end - data_start;
            lines++;
        }
        start = newline + 1;
    }
    return lines;
}

char *anthropic_frame_payload(const anthropic_frame_scan *scan, const anthropic_frame *frame, size_t *length,
                              int *out_of_memory) {
    *out_of_memory = 0;
    if (join_data_lines(scan, frame, NULL, length) == 0 || *length == 0) {
        return NULL;
    }
    char *payload = spisor_memory_allocate(*length + 1);
    if (payload == NULL) {
        *out_of_memory = 1;
        return NULL;
    }
    join_data_lines(scan, frame, payload, length);
    payload[*length] = '\0';
    return payload;
}

void anthropic_frames_commit(forebay_anthropic_stream_decoder *decoder, const anthropic_frame_scan *scan) {
    size_t kept = scan->end - scan->frame_start;
    if (kept > 0 && scan->frame_start > 0) {
        memmove(decoder->pending, decoder->pending + scan->frame_start, kept);
    }
    decoder->length = kept;
    decoder->line_start = scan->line_start - scan->frame_start;
}
