#ifndef FOREBAY_TEST_ANTHROPIC_CALL_H
#define FOREBAY_TEST_ANTHROPIC_CALL_H

#include "support.h"

#include "forebay/anthropic.h"
#include "ir_json.h"
#include "spisor/json_kind.h"
#include "vector_reader.h"

/**
 * @file
 * The anthropic service called through its definition, as a consumer calls it, with the IR and the wire JSON of the
 * vectors turned into the structs and bytes it takes and compared after the same schema form `ir_codec_test` applies.
 * A decode vector's `input` is the wire body, printed to bytes; an encode vector's `input` is the IR, read by the
 * test-only IR codec.
 */

static inline const forebay_anthropic_definition *anthropic_definition(void) {
    return (const forebay_anthropic_definition *)forebay_anthropic_definition_of();
}

/** Whether a vector's `direction` member is `direction`. */
static inline int anthropic_is_direction(const cJSON *vector, const char *direction) {
    const char *actual = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(vector, "direction"));
    return actual != NULL && strcmp(actual, direction) == 0;
}

/** The IR request of an encode vector's `input` or any IR JSON, in the arena; 1 when the codec read it. */
static inline int anthropic_read_ir(const cJSON *ir, ir_json_arena *arena, forebay_ir_request *request) {
    cJSON *form = ir_json_schema_form(ir);
    spisor_status status = form == NULL ? SPISOR_OUT_OF_MEMORY : ir_json_read_request(form, arena, request);
    cJSON_Delete(form);
    return status == SPISOR_OK;
}

static inline spisor_status anthropic_decode_text(const char *text, forebay_anthropic_request_answer *answer,
                                                  spisor_refusal *refusal) {
    forebay_anthropic_body_argument argument = SPISOR_INIT(forebay_anthropic_body_argument);
    argument.bytes = (const uint8_t *)text;
    argument.length = strlen(text);
    return anthropic_definition()->decode_request(NULL, &argument, answer, refusal);
}

static inline spisor_status anthropic_encode_request_of(const forebay_ir_request *request,
                                                        forebay_anthropic_body_answer *answer,
                                                        spisor_refusal *refusal) {
    forebay_anthropic_encode_request_argument argument = SPISOR_INIT(forebay_anthropic_encode_request_argument);
    argument.request = request;
    return anthropic_definition()->encode_request(NULL, &argument, answer, refusal);
}

/** Whether an IR request, in the schema form, is the IR JSON `expected` in its schema form. */
static inline int anthropic_request_equals(const forebay_ir_request *request, const cJSON *expected) {
    cJSON *written = NULL;
    cJSON *expected_form = ir_json_schema_form(expected);
    int same = 0;
    if (ir_json_write_request(request, &written) == SPISOR_OK) {
        cJSON *written_form = ir_json_schema_form(written);
        same = expected_form != NULL && cJSON_Compare(expected_form, written_form, 1);
        if (!same && expected_form != NULL) {
            char *wanted = cJSON_PrintUnformatted(expected_form);
            char *got = cJSON_PrintUnformatted(written_form);
            fprintf(stderr, "  expected %s\n  actual   %s\n", wanted, got);
            cJSON_free(wanted);
            cJSON_free(got);
        }
        cJSON_Delete(written_form);
    }
    cJSON_Delete(written);
    cJSON_Delete(expected_form);
    return same;
}

/** Whether a body, parsed as JSON, equals the wire JSON `expected`. */
static inline int anthropic_body_equals(const forebay_anthropic_body_answer *answer, const cJSON *expected) {
    int out_of_memory = 0;
    cJSON *parsed = spisor_parse_json_reporting(answer->bytes, answer->length, &out_of_memory);
    int same = parsed != NULL && cJSON_Compare(parsed, expected, 1);
    if (!same) {
        char *wanted = cJSON_PrintUnformatted(expected);
        fprintf(stderr, "  expected %s\n  actual   %.*s\n", wanted, (int)answer->length, (const char *)answer->bytes);
        cJSON_free(wanted);
    }
    cJSON_Delete(parsed);
    return same;
}

#endif
