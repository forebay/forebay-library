#ifndef ANTHROPIC_STOP_REASON_H
#define ANTHROPIC_STOP_REASON_H

/** The IR stop reason of a wire one the IR has no word for, and of none. */
#define ANTHROPIC_UNMAPPED_STOP_REASON "error"

/** The IR stop reason of a wire one, static text: a known one as it is, any other, or none, "error". */
const char *anthropic_stop_reason_to_ir(const char *wire_reason);

/** The wire stop reason of an IR one: "error", which the wire has no word for, is "end_turn". */
const char *anthropic_stop_reason_to_wire(const char *reason);

#endif
