#ifndef ANTHROPIC_FRAMES_H
#define ANTHROPIC_FRAMES_H

#include <stddef.h>
#include <stdint.h>

#include "forebay/anthropic.h"

/**
 * The text of a stream not yet read: the frame not yet ended, from its first line. Every line before `line_start`
 * ended, none of them blank; the line from `line_start` on has no LF yet.
 */
struct forebay_anthropic_stream_decoder {
    uint8_t *pending;
    size_t length;
    size_t capacity;
    size_t line_start;
};

/**
 * One pass over the pending text and a piece copied after it, finding the frames they complete, as the translator's
 * SseFrameBuffer splits them: a line ends at LF, a frame at a blank line. Beginning one may give the decoder more
 * room, its text kept; nothing else changes until anthropic_frames_commit, so a pass abandoned for want of memory
 * leaves the decoder reading as it did.
 *
 * @implNote The piece is copied past the pending text's `length`, into room the decoder already holds, so the pending
 * text itself is never written before the commit. The search for the next LF starts where the last pass stopped, since
 * the pending line holds none, so a stream fed one byte at a time is read in linear time.
 */
typedef struct anthropic_frame_scan {
    const uint8_t *text;
    size_t end;
    size_t frame_start;
    size_t line_start;
    size_t search_from;
} anthropic_frame_scan;

/** One frame a scan completed: its lines are the text from `start` to `end`, each ending at an LF. */
typedef struct anthropic_frame {
    size_t start;
    size_t end;
} anthropic_frame;

/**
 * Begins a scan over the pending text and `bytes` of `length`, followed, when `ends` is 1, by the blank line that ends
 * a stream, so a last line and a last frame complete as the translator's flush completes them.
 *
 * @return 1, or 0 when memory for the copy runs out, the decoder unchanged.
 */
int anthropic_frames_begin(forebay_anthropic_stream_decoder *decoder, const uint8_t *bytes, size_t length, int ends,
                           anthropic_frame_scan *scan);

/** The next frame the scan completes, 0 when no more are complete. */
int anthropic_frames_next(anthropic_frame_scan *scan, anthropic_frame *frame);

/**
 * A frame's payload: the text of its `data:` lines, each without the field name and one space after it, joined with
 * LF; freed with spisor_memory_release.
 *
 * @return the payload, NULL with `*out_of_memory` 0 for a frame of no data lines or an empty payload, which yields
 * nothing, and NULL with `*out_of_memory` 1 when memory runs out.
 */
char *anthropic_frame_payload(const anthropic_frame_scan *scan, const anthropic_frame *frame, size_t *length,
                              int *out_of_memory);

/** Keeps the text after the last frame the scan completed as the decoder's pending text. Never allocates. */
void anthropic_frames_commit(forebay_anthropic_stream_decoder *decoder, const anthropic_frame_scan *scan);

#endif
