#include "anthropic_encode.h"
#include "anthropic_reader.h"

/** What the response recorded under `key` when it was read, NULL for none or JSON null. */
static const spisor_value *recorded_raw(const forebay_ir_response *response, const char *key) {
    const spisor_value *raw = anthropic_extension_of(response->extensions, key);
    return raw == NULL || raw->kind == SPISOR_VALUE_NULL ? NULL : raw;
}

/** Sets a member to what the response recorded under `key`, else to the text `fallback`. */
static void set_raw_or_text(anthropic_writer *writer, cJSON *wire, const char *member,
                            const forebay_ir_response *response, const char *key, const char *fallback) {
    const spisor_value *raw = recorded_raw(response, key);
    if (raw == NULL) {
        anthropic_set_text(writer, wire, member, fallback);
    } else {
        anthropic_set_value(writer, wire, member, raw, key);
    }
}

static void set_stop_sequence(anthropic_writer *writer, cJSON *wire, const forebay_ir_response *response) {
    const spisor_value *raw = recorded_raw(response, ANTHROPIC_EXTENSION_STOP_SEQUENCE_RAW);
    anthropic_set_value(writer, wire, "stop_sequence", raw == NULL ? anthropic_null_value() : raw,
                        ANTHROPIC_EXTENSION_STOP_SEQUENCE_RAW);
}

cJSON *anthropic_encode_response_object(anthropic_writer *writer, const forebay_ir_response *response) {
    cJSON *wire = anthropic_new_object(writer);
    anthropic_set_text(writer, wire, "id", response->id);
    set_raw_or_text(writer, wire, "type", response, ANTHROPIC_EXTENSION_TYPE_RAW, "message");
    set_raw_or_text(writer, wire, "role", response, ANTHROPIC_EXTENSION_ROLE_RAW, "assistant");
    anthropic_set(writer, wire, "content",
                  anthropic_encode_block_list(writer, response->content, response->content_count));
    anthropic_set_text(writer, wire, "model", response->model);
    set_raw_or_text(writer, wire, "stop_reason", response, ANTHROPIC_EXTENSION_STOP_REASON_RAW,
                    response->stop_reason);
    set_stop_sequence(writer, wire, response);
    if (response->usage != NULL) {
        anthropic_set(writer, wire, "usage", anthropic_encode_usage(writer, response->usage));
    }
    anthropic_set_extensions(writer, wire, response->extensions, 0);
    return wire;
}
