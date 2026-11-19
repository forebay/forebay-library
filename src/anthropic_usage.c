#include "anthropic_decode.h"
#include "anthropic_encode.h"
#include "anthropic_wire.h"

static void read_count(const cJSON *wire, const char *key, uint32_t *has, uint64_t *count) {
    *has = (uint32_t)anthropic_wire_integer(anthropic_wire_member(wire, key), count);
}

static void write_count(anthropic_writer *writer, cJSON *wire, const char *key, uint32_t has, uint64_t count) {
    if (has) {
        anthropic_set_count(writer, wire, key, count);
    }
}

const forebay_ir_usage *anthropic_decode_usage(anthropic_reader *reader, const cJSON *wire) {
    if (anthropic_wire_map(wire) == NULL) {
        return NULL;
    }
    forebay_ir_usage *usage = anthropic_take(reader, 1, sizeof *usage);
    if (usage != NULL) {
        read_count(wire, "input_tokens", &usage->has_input_tokens, &usage->input_tokens);
        read_count(wire, "output_tokens", &usage->has_output_tokens, &usage->output_tokens);
        read_count(wire, "cache_creation_input_tokens", &usage->has_cache_creation_input_tokens,
                   &usage->cache_creation_input_tokens);
        read_count(wire, "cache_read_input_tokens", &usage->has_cache_read_input_tokens,
                   &usage->cache_read_input_tokens);
    }
    return usage;
}

cJSON *anthropic_encode_usage(anthropic_writer *writer, const forebay_ir_usage *usage) {
    cJSON *wire = anthropic_new_object(writer);
    write_count(writer, wire, "input_tokens", usage->has_input_tokens, usage->input_tokens);
    write_count(writer, wire, "output_tokens", usage->has_output_tokens, usage->output_tokens);
    write_count(writer, wire, "cache_creation_input_tokens", usage->has_cache_creation_input_tokens,
                usage->cache_creation_input_tokens);
    write_count(writer, wire, "cache_read_input_tokens", usage->has_cache_read_input_tokens,
                usage->cache_read_input_tokens);
    return wire;
}
