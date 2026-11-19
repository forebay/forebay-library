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

/** One text block holding `text`, as a string `content` or `system` is read. */
void anthropic_wrap_text(anthropic_reader *reader, const char *text, const forebay_ir_block **blocks, size_t *count);

#endif
