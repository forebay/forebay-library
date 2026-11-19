#ifndef FOREBAY_TEST_IR_JSON_NAMES_H
#define FOREBAY_TEST_IR_JSON_NAMES_H

#include <stdint.h>

/** The JSON `kind` of a forebay_ir_block_kind; NULL for a value the header does not name. */
const char *ir_json_block_kind_name(uint32_t kind);

/** The forebay_ir_block_kind a JSON `kind` names; 0 for none. */
uint32_t ir_json_block_kind_of(const char *name);

/** The JSON `event` of a forebay_ir_event_kind; NULL for a value the header does not name. */
const char *ir_json_event_kind_name(uint32_t kind);

/** The forebay_ir_event_kind a JSON `event` names; 0 for none. */
uint32_t ir_json_event_kind_of(const char *name);

/** Whether a content block start may name the block kind: text, tool use or thinking. */
int ir_json_is_started_block_kind(uint32_t kind);

#endif
