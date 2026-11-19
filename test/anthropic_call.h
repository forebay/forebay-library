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

/** Whether the IR JSON `written`, in the schema form, is the IR JSON `expected` in its schema form. */
static inline int anthropic_ir_json_equals(const cJSON *written, const cJSON *expected) {
    cJSON *written_form = ir_json_schema_form(written);
    cJSON *expected_form = ir_json_schema_form(expected);
    int same = written_form != NULL && expected_form != NULL && cJSON_Compare(expected_form, written_form, 1);
    if (!same && written_form != NULL && expected_form != NULL) {
        char *wanted = cJSON_PrintUnformatted(expected_form);
        char *got = cJSON_PrintUnformatted(written_form);
        fprintf(stderr, "  expected %s\n  actual   %s\n", wanted, got);
        cJSON_free(wanted);
        cJSON_free(got);
    }
    cJSON_Delete(written_form);
    cJSON_Delete(expected_form);
    return same;
}

/** Whether an IR request, in the schema form, is the IR JSON `expected` in its schema form. */
static inline int anthropic_request_equals(const forebay_ir_request *request, const cJSON *expected) {
    cJSON *written = NULL;
    int same = ir_json_write_request(request, &written) == SPISOR_OK && anthropic_ir_json_equals(written, expected);
    cJSON_Delete(written);
    return same;
}

/** Whether an IR response, in the schema form, is the IR JSON `expected` in its schema form. */
static inline int anthropic_response_equals(const forebay_ir_response *response, const cJSON *expected) {
    cJSON *written = NULL;
    int same = ir_json_write_response(response, &written) == SPISOR_OK && anthropic_ir_json_equals(written, expected);
    cJSON_Delete(written);
    return same;
}

/** The IR response of an encode vector's `input` or any IR JSON, in the arena; 1 when the codec read it. */
static inline int anthropic_read_response_ir(const cJSON *ir, ir_json_arena *arena, forebay_ir_response *response) {
    cJSON *form = ir_json_schema_form(ir);
    spisor_status status = form == NULL ? SPISOR_OUT_OF_MEMORY : ir_json_read_response(form, arena, response);
    cJSON_Delete(form);
    return status == SPISOR_OK;
}

static inline spisor_status anthropic_decode_response_text(const char *text,
                                                           forebay_anthropic_response_answer *answer,
                                                           spisor_refusal *refusal) {
    forebay_anthropic_body_argument argument = SPISOR_INIT(forebay_anthropic_body_argument);
    argument.bytes = (const uint8_t *)text;
    argument.length = strlen(text);
    return anthropic_definition()->decode_response(NULL, &argument, answer, refusal);
}

static inline spisor_status anthropic_encode_response_of(const forebay_ir_response *response,
                                                         forebay_anthropic_body_answer *answer,
                                                         spisor_refusal *refusal) {
    forebay_anthropic_encode_response_argument argument = SPISOR_INIT(forebay_anthropic_encode_response_argument);
    argument.response = response;
    return anthropic_definition()->encode_response(NULL, &argument, answer, refusal);
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

/** What a direction is called on: a decode's wire text, or an encode's IR read into the arena. */
typedef struct anthropic_input {
    const char *text;
    char *printed;
    ir_json_arena arena;
    forebay_ir_request request;
    forebay_ir_response response;
} anthropic_input;

/** Any answer a direction fills, begun empty with the size of its own kind by anthropic_start_answer. */
typedef union anthropic_answer {
    uint32_t size;
    forebay_anthropic_request_answer request;
    forebay_anthropic_response_answer response;
    forebay_anthropic_body_answer body;
} anthropic_answer;

/**
 * One direction of the service as a vector names it: how its input is read from the vector, the call through the
 * definition, the comparison of what it answered with the vector's `output`, and the release of that answer.
 */
typedef struct anthropic_direction {
    const char *name;
    uint32_t answer_size;
    int (*read_input)(const cJSON *json, anthropic_input *input);
    spisor_status (*call)(const anthropic_input *input, anthropic_answer *answer, spisor_refusal *refusal);
    int (*matches)(const anthropic_answer *answer, const cJSON *expected);
    void (*release)(anthropic_answer *answer);
} anthropic_direction;

/** An input holding `text`, borrowed, as a decode reads it. */
static inline anthropic_input anthropic_input_of_text(const char *text) {
    anthropic_input input = {0};
    input.text = text;
    return input;
}

static inline void anthropic_input_release(anthropic_input *input) {
    cJSON_free(input->printed);
    ir_json_arena_release(&input->arena);
    *input = (anthropic_input){0};
}

/** Zeroes the answer whole and sets the size of the direction's kind. */
static inline void anthropic_start_answer(const anthropic_direction *direction, anthropic_answer *answer) {
    memset(answer, 0, sizeof *answer);
    answer->size = direction->answer_size;
}

static inline int anthropic_read_wire_text(const cJSON *json, anthropic_input *input) {
    *input = (anthropic_input){0};
    input->printed = cJSON_PrintUnformatted(json);
    input->text = input->printed;
    return input->printed != NULL;
}

static inline int anthropic_read_request_input(const cJSON *json, anthropic_input *input) {
    *input = (anthropic_input){0};
    return anthropic_read_ir(json, &input->arena, &input->request);
}

static inline int anthropic_read_response_input(const cJSON *json, anthropic_input *input) {
    *input = (anthropic_input){0};
    return anthropic_read_response_ir(json, &input->arena, &input->response);
}

static inline spisor_status anthropic_call_decode_request(const anthropic_input *input, anthropic_answer *answer,
                                                          spisor_refusal *refusal) {
    return anthropic_decode_text(input->text, &answer->request, refusal);
}

static inline spisor_status anthropic_call_encode_request(const anthropic_input *input, anthropic_answer *answer,
                                                          spisor_refusal *refusal) {
    return anthropic_encode_request_of(&input->request, &answer->body, refusal);
}

static inline spisor_status anthropic_call_decode_response(const anthropic_input *input, anthropic_answer *answer,
                                                           spisor_refusal *refusal) {
    return anthropic_decode_response_text(input->text, &answer->response, refusal);
}

static inline spisor_status anthropic_call_encode_response(const anthropic_input *input, anthropic_answer *answer,
                                                           spisor_refusal *refusal) {
    return anthropic_encode_response_of(&input->response, &answer->body, refusal);
}

static inline int anthropic_request_matches(const anthropic_answer *answer, const cJSON *expected) {
    return answer->request.request != NULL && anthropic_request_equals(answer->request.request, expected);
}

static inline int anthropic_response_matches(const anthropic_answer *answer, const cJSON *expected) {
    return answer->response.response != NULL && anthropic_response_equals(answer->response.response, expected);
}

static inline int anthropic_body_matches(const anthropic_answer *answer, const cJSON *expected) {
    return anthropic_body_equals(&answer->body, expected);
}

static inline void anthropic_release_request(anthropic_answer *answer) {
    anthropic_definition()->release_request(NULL, &answer->request);
}

static inline void anthropic_release_response(anthropic_answer *answer) {
    anthropic_definition()->release_response(NULL, &answer->response);
}

static inline void anthropic_release_body(anthropic_answer *answer) {
    anthropic_definition()->release_body(NULL, &answer->body);
}

/** The direction a vector's `direction` member names, NULL for one this service does not have. */
static inline const anthropic_direction *anthropic_direction_named(const char *name) {
    static const anthropic_direction directions[] = {
        {"decodeRequest", (uint32_t)sizeof(forebay_anthropic_request_answer), anthropic_read_wire_text,
         anthropic_call_decode_request, anthropic_request_matches, anthropic_release_request},
        {"encodeRequest", (uint32_t)sizeof(forebay_anthropic_body_answer), anthropic_read_request_input,
         anthropic_call_encode_request, anthropic_body_matches, anthropic_release_body},
        {"decodeResponse", (uint32_t)sizeof(forebay_anthropic_response_answer), anthropic_read_wire_text,
         anthropic_call_decode_response, anthropic_response_matches, anthropic_release_response},
        {"encodeResponse", (uint32_t)sizeof(forebay_anthropic_body_answer), anthropic_read_response_input,
         anthropic_call_encode_response, anthropic_body_matches, anthropic_release_body},
    };
    for (size_t index = 0; name != NULL && index < sizeof directions / sizeof directions[0]; index++) {
        if (strcmp(directions[index].name, name) == 0) {
            return &directions[index];
        }
    }
    return NULL;
}

/** The direction of a vector, NULL for one this service does not have. */
static inline const anthropic_direction *anthropic_direction_of(const cJSON *vector) {
    return anthropic_direction_named(
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(vector, "direction")));
}

/** Calls the direction on the input and requires OK with an answer that matches `expected`. */
static inline void anthropic_check_direction(const anthropic_direction *direction, const char *label,
                                             const anthropic_input *input, const cJSON *expected) {
    anthropic_answer answer;
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    anthropic_start_answer(direction, &answer);
    spisor_status status = direction->call(input, &answer, &refusal);
    if (status != SPISOR_OK) {
        support_failf(label, "%s answered %d", direction->name, (int)status);
    } else if (!direction->matches(&answer, expected)) {
        support_failf(label, "%s did not answer the expected one", direction->name);
    }
    direction->release(&answer);
    anthropic_definition()->release_refusal(NULL, &refusal);
}

/** Reads the vector input as the direction does, then checks the direction on it. */
static inline void anthropic_check_vector_input(const anthropic_direction *direction, const char *label,
                                                const cJSON *input_json, const cJSON *expected) {
    anthropic_input input;
    if (!direction->read_input(input_json, &input)) {
        support_fail(label, "the input is not one the test reads");
    } else {
        anthropic_check_direction(direction, label, &input, expected);
    }
    anthropic_input_release(&input);
}

#endif
