#ifndef ANTHROPIC_DECODE_H
#define ANTHROPIC_DECODE_H

#include <stddef.h>

#include "anthropic_keys.h"
#include "anthropic_reader.h"
#include "cJSON.h"
#include "forebay/ir.h"

/**
 * A wire request as an IR request in the reader's storage; NULL once memory has run out. A root that is not an
 * object is the empty request.
 */
const forebay_ir_request *anthropic_decode_request(anthropic_reader *reader, const cJSON *root);

/**
 * The blocks of a wire array: the objects in it, others skipped.
 *
 * @return 1 when `wire` is an array, 0 when it is anything else, with `*blocks` NULL and `*count` 0.
 */
int anthropic_decode_blocks(anthropic_reader *reader, const cJSON *wire, const forebay_ir_block **blocks,
                            size_t *count);

/**
 * The content of a message, a tool result or the system prompt: a string is one text block and puts `string_flag`
 * in `extensions`, so writing restores the string; a list is its blocks.
 *
 * @return 1 when `content` is a string or a list, else 0 with no blocks.
 */
int anthropic_decode_content(anthropic_reader *reader, const cJSON *content, const char *string_flag,
                             const forebay_ir_block **blocks, size_t *count, anthropic_extension_list *extensions);

/** A wire response as an IR response in the reader's storage; NULL once memory has run out. */
const forebay_ir_response *anthropic_decode_response(anthropic_reader *reader, const cJSON *root);

/**
 * The IR event of one stream frame, parsed, into `*event` and the reader's storage, as the translator's stream decoder
 * reads a frame.
 *
 * @return 1, or 0 for a frame that yields no event: no object, a type it does not know, a `ping` among them, a
 * message start without a message object, a delta without one or of a type it does not know.
 */
int anthropic_decode_stream_frame(anthropic_reader *reader, const cJSON *frame, forebay_ir_event *event);

/** The usage of a wire `usage` member; NULL when it is no object, and once memory has run out. */
const forebay_ir_usage *anthropic_decode_usage(anthropic_reader *reader, const cJSON *wire);

/** One text block holding `text`, as a string `content` or `system` is read. */
void anthropic_wrap_text(anthropic_reader *reader, const char *text, const forebay_ir_block **blocks, size_t *count);

#endif
