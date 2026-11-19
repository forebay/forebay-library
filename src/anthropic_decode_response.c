#include <string.h>

#include "anthropic_decode.h"
#include "anthropic_wire.h"

static const char *const RESPONSE_CONSUMED[] = {"id",    "type",        "role",          "content",
                                                "model", "stop_reason", "stop_sequence", "usage"};
static const char *const KNOWN_STOP_REASONS[] = {"end_turn", "max_tokens", "tool_use", "stop_sequence", "pause_turn",
                                                 "refusal"};
static const char UNMAPPED_STOP_REASON[] = "error";

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

/** The IR stop reason of a wire one: a known one as it is, any other, or none, "error". */
static const char *stop_reason_to_ir(const char *wire_reason) {
    const char *known = known_stop_reason(wire_reason);
    return known != NULL ? known : UNMAPPED_STOP_REASON;
}

/** Records what the wire said that the IR cannot hold, so writing restores it; a stop reason of no string is null. */
static void put_raw_members(anthropic_reader *reader, const cJSON *root, anthropic_extension_list *extensions) {
    const cJSON *stop_reason = anthropic_wire_member(root, "stop_reason");
    anthropic_extensions_put_json(reader, extensions, ANTHROPIC_EXTENSION_STOP_REASON_RAW,
                                  anthropic_wire_string(stop_reason) == NULL ? NULL : stop_reason);
    anthropic_extensions_put_json(reader, extensions, ANTHROPIC_EXTENSION_STOP_SEQUENCE_RAW,
                                  anthropic_wire_member(root, "stop_sequence"));
    anthropic_extensions_put_json(reader, extensions, ANTHROPIC_EXTENSION_TYPE_RAW,
                                  anthropic_wire_member(root, "type"));
    anthropic_extensions_put_json(reader, extensions, ANTHROPIC_EXTENSION_ROLE_RAW,
                                  anthropic_wire_member(root, "role"));
}

static void decode_members(anthropic_reader *reader, const cJSON *root, forebay_ir_response *response) {
    anthropic_extension_list extensions;
    anthropic_extensions_begin(reader, &extensions, (size_t)cJSON_GetArraySize(root));
    response->id = anthropic_copy_text(reader, anthropic_wire_string_or_empty(anthropic_wire_member(root, "id")));
    response->model = anthropic_copy_text(reader, anthropic_wire_string_or_empty(anthropic_wire_member(root, "model")));
    anthropic_decode_blocks(reader, anthropic_wire_member(root, "content"), &response->content,
                            &response->content_count);
    response->stop_reason = stop_reason_to_ir(anthropic_wire_string(anthropic_wire_member(root, "stop_reason")));
    response->usage = anthropic_decode_usage(reader, anthropic_wire_member(root, "usage"));
    put_raw_members(reader, root, &extensions);
    anthropic_extensions_put_unconsumed(reader, &extensions, root, ANTHROPIC_KEYS_OF(RESPONSE_CONSUMED));
    response->extensions = anthropic_extensions_finish(reader, &extensions);
}

const forebay_ir_response *anthropic_decode_response(anthropic_reader *reader, const cJSON *root) {
    forebay_ir_response *response = anthropic_take(reader, 1, sizeof *response);
    if (response == NULL) {
        return NULL;
    }
    response->size = (uint32_t)sizeof *response;
    response->id = "";
    response->model = "";
    response->stop_reason = UNMAPPED_STOP_REASON;
    if (anthropic_wire_map(root) != NULL) {
        decode_members(reader, root, response);
    }
    return reader->out_of_memory ? NULL : response;
}
