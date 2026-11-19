#include <string.h>

#include "anthropic_stop_reason.h"

static const char *const KNOWN_STOP_REASONS[] = {"end_turn", "max_tokens", "tool_use", "stop_sequence", "pause_turn",
                                                 "refusal"};

/** The static text of the known stop reason `wire_reason` names, NULL for any other or none. */
static const char *known_stop_reason(const char *wire_reason) {
    for (size_t index = 0; wire_reason != NULL && index < sizeof KNOWN_STOP_REASONS / sizeof KNOWN_STOP_REASONS[0];
         index++) {
        if (strcmp(wire_reason, KNOWN_STOP_REASONS[index]) == 0) {
            return KNOWN_STOP_REASONS[index];
        }
    }
    return NULL;
}

const char *anthropic_stop_reason_to_ir(const char *wire_reason) {
    const char *known = known_stop_reason(wire_reason);
    return known != NULL ? known : ANTHROPIC_UNMAPPED_STOP_REASON;
}

const char *anthropic_stop_reason_to_wire(const char *reason) {
    return strcmp(reason, ANTHROPIC_UNMAPPED_STOP_REASON) == 0 ? "end_turn" : reason;
}
