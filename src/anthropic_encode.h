#ifndef ANTHROPIC_ENCODE_H
#define ANTHROPIC_ENCODE_H

#include <stddef.h>

#include "anthropic_keys.h"
#include "anthropic_writer.h"
#include "cJSON.h"
#include "forebay/ir.h"

/** The wire object of an IR request that anthropic_check_request accepted; NULL once the writer has failed. */
cJSON *anthropic_encode_request_object(anthropic_writer *writer, const forebay_ir_request *request);

/** The wire array of IR blocks; an absent list is empty. NULL once the writer has failed. */
cJSON *anthropic_encode_block_list(anthropic_writer *writer, const forebay_ir_block *blocks, size_t count);

/**
 * Whether `blocks` are written back as a string: `extensions` has `flag` set, which says the string was read as one
 * text block, and that block is still plain, with no cache control and no extensions.
 */
int anthropic_is_written_as_string(const spisor_value *extensions, const char *flag, const forebay_ir_block *blocks,
                                   size_t count);

/** Sets `key` to the blocks as a string when anthropic_is_written_as_string says so, else as the block list. */
void anthropic_set_content(anthropic_writer *writer, cJSON *object, const char *key, const spisor_value *extensions,
                           const char *string_flag, const forebay_ir_block *blocks, size_t count);

/**
 * Sets the members of an extensions record that are the wire's own, those whose key does not begin with `$`.
 *
 * @implNote The translator keeps a tool's and a tool choice's extensions whole, `$` keys included, since it assigns
 * them where it filters the others; `include_translator_keys` is that difference.
 */
void anthropic_set_extensions(anthropic_writer *writer, cJSON *object, const spisor_value *extensions,
                              int include_translator_keys);

#endif
