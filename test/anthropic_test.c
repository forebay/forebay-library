#include "anthropic_call.h"
#include "anthropic_shape_cases.h"

#include "spisor/json_allocation.h"

/**
 * @file
 * The anthropic service's translation through its definition: every `request-*` and `response-*` vector in both
 * directions (a
 * decode vector's `input` as bytes against its `output`, an encode vector's `input` against its `output`), then the
 * cases no vector writes: wire members of the wrong type, bytes that are not JSON, a root that is not an object, an IR
 * that breaks its header, and the byte form of a body.
 */

#define REQUEST_VECTOR_COUNT 18
#define RESPONSE_VECTOR_COUNT 16

static cJSON *parse(const char *text) {
    int out_of_memory = 0;
    return spisor_parse_json_reporting((const uint8_t *)text, strlen(text), &out_of_memory);
}

/** Checks the direction named `name` on a decode's wire `body`. */
static void check_text(const char *name, const char *label, const char *body, const cJSON *expected) {
    anthropic_input input = anthropic_input_of_text(body);
    anthropic_check_direction(anthropic_direction_named(name), label, &input, expected);
}

/** Checks the direction named `name` on an encode's IR JSON `ir`. */
static void check_ir(const char *name, const char *label, const cJSON *ir, const cJSON *expected) {
    anthropic_check_vector_input(anthropic_direction_named(name), label, ir, expected);
}

static void run_vector(const char *path) {
    cJSON *vector = spisor_vector_read(path);
    const cJSON *input = cJSON_GetObjectItemCaseSensitive(vector, "input");
    const cJSON *output = cJSON_GetObjectItemCaseSensitive(vector, "output");
    const anthropic_direction *direction = anthropic_direction_of(vector);
    if (vector == NULL || input == NULL || output == NULL) {
        support_fail(path, "not a vector with an input and an output");
    } else if (direction == NULL) {
        support_fail(path, "a vector of no direction this service has");
    } else {
        anthropic_check_vector_input(direction, path, input, output);
    }
    cJSON_Delete(vector);
}

static int run_vectors(const char *prefix) {
    char *list = support_read_text(ANTHROPIC_VECTOR_LIST);
    int count = 0;
    if (list == NULL) {
        support_fail("vector list", ANTHROPIC_VECTOR_LIST);
        return 0;
    }
    for (char *line = strtok(list, "\r\n"); line != NULL; line = strtok(NULL, "\r\n")) {
        if (strstr(line, prefix) != NULL) {
            run_vector(line);
            count++;
        }
    }
    free(list);
    return count;
}

typedef struct decode_case {
    const char *label;
    const char *body;
    const char *expected;
} decode_case;

static const decode_case DECODE_CASES[] = {
    {"a member of the wrong type reads as absent",
     "{\"model\":5,\"messages\":\"x\",\"stream\":\"yes\",\"max_tokens\":\"8\",\"temperature\":\"hot\",\"tools\":{},"
     "\"tool_choice\":[],\"thinking\":\"on\",\"system\":7,\"stop_sequences\":\"a\",\"metadata\":[]}",
     "{\"model\":\"\",\"messages\":[],\"stream\":false}"},
    {"a count outside the range reads as absent",
     "{\"model\":\"m\",\"messages\":[],\"max_tokens\":-5,\"top_k\":1e30,\"top_p\":0.5}",
     "{\"model\":\"m\",\"messages\":[],\"stream\":false,\"topP\":0.5}"},
    {"stop sequences read as JavaScript's String does",
     "{\"model\":\"m\",\"messages\":[],\"stop_sequences\":[\"a\",1,null,true,[\"x\",null],{}]}",
     "{\"model\":\"m\",\"messages\":[],\"stream\":false,"
     "\"stopSequences\":[\"a\",\"1\",\"null\",\"true\",\"x,\",\"[object Object]\"]}"},
    {"a cache control keeps its other members",
     "{\"model\":\"m\",\"messages\":[{\"role\":\"user\",\"content\":[{\"type\":\"text\",\"text\":\"t\","
     "\"cache_control\":{\"type\":\"ephemeral\",\"ttl\":\"1h\"}}]}]}",
     "{\"model\":\"m\",\"stream\":false,\"messages\":[{\"role\":\"user\",\"content\":[{\"kind\":\"text\","
     "\"text\":\"t\",\"cacheControl\":\"ephemeral\",\"extensions\":{\"$cacheControlExtra\":{\"ttl\":\"1h\"}}}]}]}"},
    {"a cache control whose type is no string has no type",
     "{\"model\":\"m\",\"messages\":[{\"role\":\"user\",\"content\":[{\"type\":\"text\",\"text\":\"t\","
     "\"cache_control\":{\"type\":5}},{\"type\":\"text\",\"text\":\"u\",\"cache_control\":\"x\"}]}]}",
     "{\"model\":\"m\",\"stream\":false,\"messages\":[{\"role\":\"user\",\"content\":[{\"kind\":\"text\","
     "\"text\":\"t\"},{\"kind\":\"text\",\"text\":\"u\"}]}]}"},
    {"a wire key named like a translator key takes its place",
     "{\"model\":\"m\",\"messages\":[],\"system\":\"s\",\"$systemIsString\":\"nope\"}",
     "{\"model\":\"m\",\"messages\":[],\"stream\":false,\"system\":[{\"kind\":\"text\",\"text\":\"s\"}],"
     "\"extensions\":{\"$systemIsString\":\"nope\"}}"},
    {"every image source",
     "{\"model\":\"m\",\"messages\":[{\"role\":\"user\",\"content\":[{\"type\":\"image\","
     "\"source\":{\"type\":\"base64\","
     "\"media_type\":\"image/png\",\"data\":\"AA\",\"extra\":1}},{\"type\":\"image\",\"source\":{\"type\":\"url\","
     "\"url\":\"u\"}},{\"type\":\"image\",\"source\":{\"type\":\"file\",\"id\":\"x\"}},{\"type\":\"image\"}]}]}",
     "{\"model\":\"m\",\"stream\":false,\"messages\":[{\"role\":\"user\",\"content\":[{\"kind\":\"image\","
     "\"mediaType\":\"image/png\",\"data\":\"AA\"},{\"kind\":\"image\",\"mediaType\":\"\",\"url\":\"u\"},"
     "{\"kind\":\"image\",\"mediaType\":\"\",\"extensions\":{\"$imageSourceRaw\":{\"type\":\"file\",\"id\":\"x\"}}},"
     "{\"kind\":\"image\",\"mediaType\":\"\"}]}]}"},
    {"a number that is not finite reads as null",
     "{\"model\":\"m\",\"messages\":[],\"x\":1e400,\"tools\":[{\"name\":\"n\",\"input_schema\":{\"a\":[-1e400]}}]}",
     "{\"model\":\"m\",\"messages\":[],\"stream\":false,\"extensions\":{\"x\":null},\"tools\":[{\"name\":\"n\","
     "\"inputSchema\":{\"a\":[null]}}]}"},
    {"items of a list that are no object are dropped",
     "{\"model\":\"m\",\"messages\":[1,{\"role\":\"user\",\"content\":[\"x\",{\"type\":\"text\","
     "\"text\":\"t\"},null]}],"
     "\"tools\":[3,{\"name\":\"n\"}]}",
     "{\"model\":\"m\",\"stream\":false,\"tools\":[{\"name\":\"n\",\"inputSchema\":null}],\"messages\":[{\"role\":"
     "\"user\",\"content\":[{\"kind\":\"text\",\"text\":\"t\"}]}]}"},
};

static void run_decode_cases(void) {
    for (size_t index = 0; index < sizeof DECODE_CASES / sizeof DECODE_CASES[0]; index++) {
        cJSON *expected = parse(DECODE_CASES[index].expected);
        if (expected == NULL) {
            support_fail(DECODE_CASES[index].label, "the expected IR is not JSON");
        } else {
            check_text("decodeRequest", DECODE_CASES[index].label, DECODE_CASES[index].body, expected);
        }
        cJSON_Delete(expected);
    }
}

/** The wire body of 1e400, which JSON.stringify writes as null, survives decoding and encoding as null. */
static void a_number_that_is_not_finite_is_written_as_null(void) {
    forebay_anthropic_request_answer decoded = SPISOR_INIT(forebay_anthropic_request_answer);
    forebay_anthropic_body_answer encoded = SPISOR_INIT(forebay_anthropic_body_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    cJSON *expected = parse("{\"model\":\"m\",\"messages\":[],\"stream\":false,\"x\":null,"
                            "\"tools\":[{\"name\":\"n\",\"input_schema\":{\"a\":[null]}}]}");
    if (anthropic_decode_text("{\"model\":\"m\",\"messages\":[],\"x\":1e400,\"tools\":[{\"name\":\"n\","
                              "\"input_schema\":{\"a\":[1e400]}}]}", &decoded, &refusal) != SPISOR_OK ||
        anthropic_encode_request_of(decoded.request, &encoded, &refusal) != SPISOR_OK ||
        !anthropic_body_equals(&encoded, expected)) {
        support_fail("a number that is not finite", "did not round trip as null");
    }
    anthropic_definition()->release_body(NULL, &encoded);
    anthropic_definition()->release_request(NULL, &decoded);
    anthropic_definition()->release_refusal(NULL, &refusal);
    cJSON_Delete(expected);
}

static void a_root_that_is_no_object_decodes_to_the_empty_request(void) {
    static const char *const roots[] = {"[]", "5", "\"x\"", "null", "true", "[{\"model\":\"m\"}]"};
    cJSON *empty = parse("{\"model\":\"\",\"messages\":[],\"stream\":false}");
    for (size_t index = 0; index < sizeof roots / sizeof roots[0]; index++) {
        check_text("decodeRequest", roots[index], roots[index], empty);
    }
    cJSON_Delete(empty);
}

static const char *const NON_JSON_BODIES[] = {"not json", "", "{", "[1,2", "{\"a\":1} x", "\xff\xfe",
                                              "{\"a\":\"\\u0000\"}"};

static void expect_not_json_refusal(const char *body, spisor_status status, const spisor_refusal *refusal,
                                    const void *answered) {
    if (status != SPISOR_MALFORMED || refusal->detail == NULL || refusal->fix == NULL ||
        strstr(refusal->fix, "JSON") == NULL || answered != NULL) {
        support_failf("bytes that are no JSON", "\"%s\" answered %d", body, (int)status);
    }
}

static void bytes_that_are_no_json_are_malformed(void) {
    for (size_t index = 0; index < sizeof NON_JSON_BODIES / sizeof NON_JSON_BODIES[0]; index++) {
        forebay_anthropic_request_answer answer = SPISOR_INIT(forebay_anthropic_request_answer);
        spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
        spisor_status status = anthropic_decode_text(NON_JSON_BODIES[index], &answer, &refusal);
        expect_not_json_refusal(NON_JSON_BODIES[index], status, &refusal, answer.storage);
        anthropic_definition()->release_refusal(NULL, &refusal);
        forebay_anthropic_response_answer response_answer = SPISOR_INIT(forebay_anthropic_response_answer);
        status = anthropic_decode_response_text(NON_JSON_BODIES[index], &response_answer, &refusal);
        expect_not_json_refusal(NON_JSON_BODIES[index], status, &refusal, response_answer.storage);
        anthropic_definition()->release_refusal(NULL, &refusal);
    }
}

static void a_string_holding_nul_is_refused_as_unreadable_json(void) {
    forebay_anthropic_request_answer answer = SPISOR_INIT(forebay_anthropic_request_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    spisor_status status = anthropic_decode_text("{\"a\":\"\\u0000\"}", &answer, &refusal);
    if (status != SPISOR_MALFORMED || refusal.detail == NULL || strstr(refusal.detail, "NUL") == NULL) {
        support_failf("a string holding NUL", "answered %d without saying NUL", (int)status);
    }
    anthropic_definition()->release_refusal(NULL, &refusal);
}

static void bytes_may_be_null_only_beside_a_length_of_zero(void) {
    forebay_anthropic_body_argument argument = SPISOR_INIT(forebay_anthropic_body_argument);
    forebay_anthropic_request_answer answer = SPISOR_INIT(forebay_anthropic_request_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    argument.length = 3;
    spisor_status status = anthropic_definition()->decode_request(NULL, &argument, &answer, &refusal);
    if (status != SPISOR_MALFORMED || refusal.detail == NULL) {
        support_failf("NULL bytes beside a length", "answered %d", (int)status);
    }
    anthropic_definition()->release_refusal(NULL, &refusal);
    argument.length = 0;
    status = anthropic_definition()->decode_request(NULL, &argument, &answer, &refusal);
    if (status != SPISOR_MALFORMED) {
        support_failf("NULL bytes and no length", "answered %d, though no body is no JSON", (int)status);
    }
    anthropic_definition()->release_refusal(NULL, &refusal);
}

typedef struct encode_case {
    const char *label;
    const char *ir;
    const char *expected;
} encode_case;

static const encode_case ENCODE_CASES[] = {
    {"a translator key never reaches the body, except on a tool and a tool choice",
     "{\"model\":\"m\",\"stream\":false,\"extensions\":{\"$wireHeaders\":{\"a\":\"b\"},\"x\":1},"
     "\"messages\":[{\"role\":\"user\",\"extensions\":{\"$k\":1,\"x\":1},\"content\":[{\"kind\":\"text\","
     "\"text\":\"t\",\"extensions\":{\"$k\":1,\"x\":1}}]}],"
     "\"tools\":[{\"name\":\"n\",\"inputSchema\":{},\"extensions\":{\"$k\":1,\"y\":2}}],"
     "\"toolChoice\":{\"type\":\"tool\",\"name\":\"n\",\"extensions\":{\"$k\":1}}}",
     "{\"model\":\"m\",\"stream\":false,\"x\":1,\"messages\":[{\"role\":\"user\",\"x\":1,\"content\":[{\"type\":"
     "\"text\",\"text\":\"t\",\"x\":1}]}],\"tools\":[{\"name\":\"n\",\"input_schema\":{},\"$k\":1,\"y\":2}],"
     "\"tool_choice\":{\"type\":\"tool\",\"name\":\"n\",\"$k\":1}}"},
    {"a string content is restored only while it is one plain text block",
     "{\"model\":\"m\",\"stream\":false,\"system\":[{\"kind\":\"text\",\"text\":\"s\",\"cacheControl\":\"c\"}],"
     "\"extensions\":{\"$systemIsString\":true},\"messages\":["
     "{\"role\":\"user\",\"extensions\":{\"$contentIsString\":true},\"content\":[{\"kind\":\"text\",\"text\":\"a\"}]},"
     "{\"role\":\"user\",\"extensions\":{\"$contentIsString\":true},\"content\":[{\"kind\":\"text\",\"text\":\"b\","
     "\"extensions\":{}}]},"
     "{\"role\":\"user\",\"extensions\":{\"$contentIsString\":true},\"content\":[{\"kind\":\"text\",\"text\":\"c\","
     "\"extensions\":null}]},"
     "{\"role\":\"user\",\"extensions\":{\"$contentIsString\":1},\"content\":[{\"kind\":\"text\",\"text\":\"d\"}]},"
     "{\"role\":\"user\",\"extensions\":{\"$contentIsString\":true},\"content\":[{\"kind\":\"text\",\"text\":\"e\"},"
     "{\"kind\":\"text\",\"text\":\"f\"}]}]}",
     "{\"model\":\"m\",\"stream\":false,\"system\":[{\"type\":\"text\",\"text\":\"s\",\"cache_control\":"
     "{\"type\":\"c\"}}],\"messages\":[{\"role\":\"user\",\"content\":\"a\"},"
     "{\"role\":\"user\",\"content\":[{\"type\":\"text\",\"text\":\"b\"}]},{\"role\":\"user\",\"content\":\"c\"},"
     "{\"role\":\"user\",\"content\":[{\"type\":\"text\",\"text\":\"d\"}]},{\"role\":\"user\",\"content\":"
     "[{\"type\":\"text\",\"text\":\"e\"},{\"type\":\"text\",\"text\":\"f\"}]}]}"},
    {"a thinking type is the recorded one when it is text, else what enabled says",
     "{\"model\":\"m\",\"stream\":false,\"messages\":[],\"thinking\":{\"enabled\":false,\"budgetTokens\":5}}",
     "{\"model\":\"m\",\"messages\":[],\"stream\":false,\"thinking\":{\"type\":\"disabled\",\"budget_tokens\":5}}"},
    {"a recorded thinking type of another kind is ignored",
     "{\"model\":\"m\",\"stream\":false,\"messages\":[],\"thinking\":{\"enabled\":true},"
     "\"extensions\":{\"$thinkingType\":5}}",
     "{\"model\":\"m\",\"messages\":[],\"stream\":false,\"thinking\":{\"type\":\"enabled\"}}"},
    {"a recorded thinking type is written",
     "{\"model\":\"m\",\"stream\":false,\"messages\":[],\"thinking\":{\"enabled\":true},"
     "\"extensions\":{\"$thinkingType\":\"adaptive\"}}",
     "{\"model\":\"m\",\"messages\":[],\"stream\":false,\"thinking\":{\"type\":\"adaptive\"}}"},
    {"a null metadata is not written",
     "{\"model\":\"m\",\"stream\":false,\"messages\":[],\"metadata\":null}",
     "{\"model\":\"m\",\"messages\":[],\"stream\":false}"},
};

static void run_encode_cases(void) {
    for (size_t index = 0; index < sizeof ENCODE_CASES / sizeof ENCODE_CASES[0]; index++) {
        cJSON *ir = parse(ENCODE_CASES[index].ir);
        cJSON *expected = parse(ENCODE_CASES[index].expected);
        if (ir == NULL || expected == NULL) {
            support_fail(ENCODE_CASES[index].label, "the case is not JSON");
        } else {
            check_ir("encodeRequest", ENCODE_CASES[index].label, ir, expected);
        }
        cJSON_Delete(expected);
        cJSON_Delete(ir);
    }
}

/** The bytes of a body, which JSON equality cannot see: member order as JSON.stringify gives it, numbers, escapes. */
static void a_body_has_the_bytes_the_translator_writes(void) {
    const char *ir_text =
        "{\"model\":\"a\\\"b\\n\\u0001\\u00e9\",\"stream\":false,\"messages\":[],\"maxTokens\":9007199254740992,"
        "\"temperature\":0.30000000000000004,\"topP\":1e-7,\"extensions\":{\"b\":1,\"2\":2,\"1\":3}}";
    static const char expected[] =
        "{\"1\":3,\"2\":2,\"model\":\"a\\\"b\\n\\u0001\xc3\xa9\",\"messages\":[],\"max_tokens\":9007199254740992,"
        "\"temperature\":0.30000000000000004,\"top_p\":1e-7,\"stream\":false,\"b\":1}";
    cJSON *ir = parse(ir_text);
    ir_json_arena arena = {0};
    forebay_ir_request request;
    forebay_anthropic_body_answer answer = SPISOR_INIT(forebay_anthropic_body_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    if (ir == NULL || !anthropic_read_ir(ir, &arena, &request) ||
        anthropic_encode_request_of(&request, &answer, &refusal) != SPISOR_OK) {
        support_fail("the bytes of a body", "the request was not encoded");
    } else if (answer.length != sizeof expected - 1 || memcmp(answer.bytes, expected, answer.length) != 0 ||
               answer.bytes[answer.length] != '\0') {
        support_failf("the bytes of a body", "wrote %.*s", (int)answer.length, (const char *)answer.bytes);
    }
    anthropic_definition()->release_body(NULL, &answer);
    anthropic_definition()->release_refusal(NULL, &refusal);
    ir_json_arena_release(&arena);
    cJSON_Delete(ir);
}

static void expect_malformed(const char *label, const forebay_ir_request *request) {
    forebay_anthropic_body_answer answer = SPISOR_INIT(forebay_anthropic_body_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    spisor_status status = anthropic_encode_request_of(request, &answer, &refusal);
    if (status != SPISOR_MALFORMED || refusal.detail == NULL || refusal.fix == NULL || answer.storage != NULL ||
        answer.bytes != NULL) {
        support_failf(label, "answered %d, not MALFORMED with a refusal and an empty answer", (int)status);
    }
    anthropic_definition()->release_refusal(NULL, &refusal);
}

static forebay_ir_request valid_request(void) {
    forebay_ir_request request = SPISOR_INIT(forebay_ir_request);
    request.model = "m";
    return request;
}

static void an_ir_that_breaks_its_header_is_malformed(void) {
    static const forebay_ir_message message = {"user", NULL, 0, NULL};
    static const forebay_ir_block unnamed_kind = {.kind = 99};
    static const forebay_ir_block unknown_without_raw = {.kind = FOREBAY_IR_BLOCK_UNKNOWN};
    static const forebay_ir_block text_without_text = {.kind = FOREBAY_IR_BLOCK_TEXT};
    static const forebay_ir_message block_message_kind = {"user", &unnamed_kind, 1, NULL};
    static const forebay_ir_message block_message_raw = {"user", &unknown_without_raw, 1, NULL};
    static const forebay_ir_message block_message_text = {"user", &text_without_text, 1, NULL};
    static const spisor_value unnamed_value = {99, 0, 0, NULL, 0, NULL, NULL, 0};
    forebay_ir_request request = valid_request();
    request.size = (uint32_t)sizeof request.size;
    expect_malformed("a request short of its first shape", &request);
    expect_malformed("a NULL request", NULL);
    request = valid_request();
    request.model = NULL;
    expect_malformed("a request without a model", &request);
    request = valid_request();
    request.message_count = 1;
    expect_malformed("messages NULL beside a count", &request);
    request = valid_request();
    request.messages = &message;
    request.message_count = 1;
    request.stop_sequence_count = 1;
    expect_malformed("stop sequences NULL beside a count", &request);
    request = valid_request();
    request.messages = &block_message_kind;
    request.message_count = 1;
    expect_malformed("a block kind the header does not name", &request);
    request.messages = &block_message_raw;
    expect_malformed("an unknown block without raw", &request);
    request.messages = &block_message_text;
    expect_malformed("a text block without text", &request);
    request = valid_request();
    request.metadata = &unnamed_value;
    expect_malformed("a metadata value tree that spisor_value_check refuses", &request);
}

/** Value trees a request holds in places the encoder reads at once: each must be MALFORMED, never a crash. */
static void an_extension_tree_that_is_not_well_formed_is_malformed(void) {
    static const spisor_value empty_text = {SPISOR_VALUE_TEXT, 0, 0, NULL, 0, NULL, NULL, 0};
    static const spisor_value no_entries = {SPISOR_VALUE_RECORD, 0, 0, NULL, 0, NULL, NULL, 2};
    static const spisor_entry unkeyed[] = {{NULL, 0, {SPISOR_VALUE_NULL, 0, 0, NULL, 0, NULL, NULL, 0}}};
    static const spisor_value unkeyed_record = {SPISOR_VALUE_RECORD, 0, 0, NULL, 0, NULL, unkeyed, 1};
    static const spisor_entry thinking_type[] = {
        {"$thinkingType", 13, {SPISOR_VALUE_TEXT, 0, 0, NULL, 0, NULL, NULL, 0}}};
    static const spisor_value thinking_record = {SPISOR_VALUE_RECORD, 0, 0, NULL, 0, NULL, thinking_type, 1};
    static const spisor_entry short_key[] = {
        {"abc", 1, {SPISOR_VALUE_NULL, 0, 0, NULL, 0, NULL, NULL, 0}}};
    static const spisor_value short_key_record = {SPISOR_VALUE_RECORD, 0, 0, NULL, 0, NULL, short_key, 1};
    static const forebay_ir_thinking thinking = {1, 0, 0};
    static const forebay_ir_block raw_block = {.kind = FOREBAY_IR_BLOCK_UNKNOWN, .raw = &no_entries};
    static const forebay_ir_message raw_message = {"user", &raw_block, 1, NULL};
    static const spisor_value *const bad[] = {&empty_text, &no_entries, &unkeyed_record, &thinking_record,
                                              &short_key_record};
    static const char *const names[] = {"extensions text without bytes", "extensions record without entries",
                                        "extensions entry without a key", "a $thinkingType text without bytes",
                                        "extensions key shorter than its length"};
    for (size_t index = 0; index < sizeof bad / sizeof bad[0]; index++) {
        forebay_ir_request request = valid_request();
        forebay_ir_message message = {"user", NULL, 0, bad[index]};
        request.extensions = bad[index];
        request.thinking = &thinking;
        expect_malformed(names[index], &request);
        request = valid_request();
        request.messages = &message;
        request.message_count = 1;
        expect_malformed(names[index], &request);
    }
    forebay_ir_request request = valid_request();
    request.messages = &raw_message;
    request.message_count = 1;
    expect_malformed("an unknown block's raw without entries", &request);
}

typedef struct response_case {
    const char *label;
    const char *input;
    const char *expected;
} response_case;

static const response_case RESPONSE_DECODE_CASES[] = {
    {"a member of the wrong type reads as absent and is kept raw",
     "{\"id\":5,\"model\":null,\"content\":\"x\",\"stop_reason\":7,\"stop_sequence\":\"s\",\"type\":3,\"role\":[],"
     "\"usage\":\"u\"}",
     "{\"id\":\"\",\"model\":\"\",\"content\":[],\"stopReason\":\"error\",\"extensions\":{\"$stopReasonRaw\":null,"
     "\"$stopSequenceRaw\":\"s\",\"$typeRaw\":3,\"$roleRaw\":[]}}"},
    {"a usage count outside the range reads as absent",
     "{\"id\":\"i\",\"model\":\"m\",\"usage\":{\"input_tokens\":-1,\"output_tokens\":\"2\","
     "\"cache_creation_input_tokens\":1e30,\"cache_read_input_tokens\":2.9,\"x\":1}}",
     "{\"id\":\"i\",\"model\":\"m\",\"content\":[],\"stopReason\":\"error\",\"usage\":{\"cacheReadInputTokens\":2},"
     "\"extensions\":{\"$stopReasonRaw\":null,\"$stopSequenceRaw\":null,\"$typeRaw\":null,\"$roleRaw\":null}}"},
    {"a stop reason is known only by its exact text",
     "{\"id\":\"i\",\"model\":\"m\",\"stop_reason\":\"End_Turn\"}",
     "{\"id\":\"i\",\"model\":\"m\",\"content\":[],\"stopReason\":\"error\",\"extensions\":{\"$stopReasonRaw\":"
     "\"End_Turn\",\"$stopSequenceRaw\":null,\"$typeRaw\":null,\"$roleRaw\":null}}"},
    {"a tool use stop reason is kept as it is",
     "{\"id\":\"i\",\"model\":\"m\",\"stop_reason\":\"tool_use\"}",
     "{\"id\":\"i\",\"model\":\"m\",\"content\":[],\"stopReason\":\"tool_use\",\"extensions\":{\"$stopReasonRaw\":"
     "\"tool_use\",\"$stopSequenceRaw\":null,\"$typeRaw\":null,\"$roleRaw\":null}}"},
    {"a wire key named like a translator key takes its place",
     "{\"id\":\"i\",\"model\":\"m\",\"$typeRaw\":\"x\",\"type\":\"message\",\"y\":1}",
     "{\"id\":\"i\",\"model\":\"m\",\"content\":[],\"stopReason\":\"error\",\"extensions\":{\"$stopReasonRaw\":null,"
     "\"$stopSequenceRaw\":null,\"$typeRaw\":\"x\",\"$roleRaw\":null,\"y\":1}}"},
    {"a number that is not finite reads as null",
     "{\"id\":\"i\",\"model\":\"m\",\"x\":1e400,\"stop_sequence\":-1e400}",
     "{\"id\":\"i\",\"model\":\"m\",\"content\":[],\"stopReason\":\"error\",\"extensions\":{\"$stopReasonRaw\":null,"
     "\"$stopSequenceRaw\":null,\"$typeRaw\":null,\"$roleRaw\":null,\"x\":null}}"},
};

static void run_response_decode_cases(void) {
    for (size_t index = 0; index < sizeof RESPONSE_DECODE_CASES / sizeof RESPONSE_DECODE_CASES[0]; index++) {
        cJSON *expected = parse(RESPONSE_DECODE_CASES[index].expected);
        if (expected == NULL) {
            support_fail(RESPONSE_DECODE_CASES[index].label, "the expected IR is not JSON");
        } else {
            check_text("decodeResponse", RESPONSE_DECODE_CASES[index].label, RESPONSE_DECODE_CASES[index].input,
                       expected);
        }
        cJSON_Delete(expected);
    }
}

static void a_response_root_that_is_no_object_decodes_to_the_empty_response(void) {
    static const char *const roots[] = {"[]", "5", "\"x\"", "null", "true", "[{\"id\":\"i\"}]"};
    cJSON *empty = parse("{\"id\":\"\",\"model\":\"\",\"content\":[],\"stopReason\":\"error\"}");
    for (size_t index = 0; index < sizeof roots / sizeof roots[0]; index++) {
        check_text("decodeResponse", roots[index], roots[index], empty);
    }
    cJSON_Delete(empty);
}

static const response_case RESPONSE_ENCODE_CASES[] = {
    {"a response read from no wire is written with the defaults",
     "{\"id\":\"i\",\"model\":\"m\",\"content\":[],\"stopReason\":\"tool_use\"}",
     "{\"id\":\"i\",\"type\":\"message\",\"role\":\"assistant\",\"content\":[],\"model\":\"m\","
     "\"stop_reason\":\"tool_use\",\"stop_sequence\":null}"},
    {"a raw extension of any kind is written, a null one is not",
     "{\"id\":\"i\",\"model\":\"m\",\"content\":[],\"stopReason\":\"end_turn\",\"extensions\":{\"$stopReasonRaw\":null,"
     "\"$stopSequenceRaw\":\"s\",\"$typeRaw\":5,\"$roleRaw\":{\"a\":1},\"$other\":1,\"x\":2}}",
     "{\"id\":\"i\",\"type\":5,\"role\":{\"a\":1},\"content\":[],\"model\":\"m\",\"stop_reason\":\"end_turn\","
     "\"stop_sequence\":\"s\",\"x\":2}"},
    {"usage is written in the order of its counts and only those present",
     "{\"id\":\"i\",\"model\":\"m\",\"content\":[],\"stopReason\":\"end_turn\",\"usage\":{\"cacheReadInputTokens\":4,"
     "\"cacheCreationInputTokens\":3,\"outputTokens\":2,\"inputTokens\":1}}",
     "{\"id\":\"i\",\"type\":\"message\",\"role\":\"assistant\",\"content\":[],\"model\":\"m\","
     "\"stop_reason\":\"end_turn\",\"stop_sequence\":null,\"usage\":{\"input_tokens\":1,\"output_tokens\":2,"
     "\"cache_creation_input_tokens\":3,\"cache_read_input_tokens\":4}}"},
    {"an empty usage is written as an empty object",
     "{\"id\":\"i\",\"model\":\"m\",\"content\":[],\"stopReason\":\"end_turn\",\"usage\":{}}",
     "{\"id\":\"i\",\"type\":\"message\",\"role\":\"assistant\",\"content\":[],\"model\":\"m\","
     "\"stop_reason\":\"end_turn\",\"stop_sequence\":null,\"usage\":{}}"},
    {"a wire extension replaces a member of the same name where it stands",
     "{\"id\":\"i\",\"model\":\"m\",\"content\":[],\"stopReason\":\"end_turn\",\"extensions\":{\"model\":\"other\"}}",
     "{\"id\":\"i\",\"type\":\"message\",\"role\":\"assistant\",\"content\":[],\"model\":\"other\","
     "\"stop_reason\":\"end_turn\",\"stop_sequence\":null}"},
};

static void run_response_encode_cases(void) {
    for (size_t index = 0; index < sizeof RESPONSE_ENCODE_CASES / sizeof RESPONSE_ENCODE_CASES[0]; index++) {
        cJSON *ir = parse(RESPONSE_ENCODE_CASES[index].input);
        cJSON *expected = parse(RESPONSE_ENCODE_CASES[index].expected);
        if (ir == NULL || expected == NULL) {
            support_fail(RESPONSE_ENCODE_CASES[index].label, "the case is not JSON");
        } else {
            check_ir("encodeResponse", RESPONSE_ENCODE_CASES[index].label, ir, expected);
        }
        cJSON_Delete(expected);
        cJSON_Delete(ir);
    }
}

/** Decoding a body and encoding what came back writes the body again, for what the IR cannot hold as well. */
static void a_response_decoded_then_encoded_restores_the_wire(void) {
    static const char wire[] =
        "{\"id\":\"i\",\"type\":\"msg2\",\"role\":\"bot\",\"content\":[{\"type\":\"text\",\"text\":\"t\"}],"
        "\"model\":\"m\",\"stop_reason\":\"future\",\"stop_sequence\":\"s\",\"usage\":{\"input_tokens\":1},"
        "\"extra\":{\"a\":[1,null]}}";
    forebay_anthropic_response_answer decoded = SPISOR_INIT(forebay_anthropic_response_answer);
    forebay_anthropic_body_answer encoded = SPISOR_INIT(forebay_anthropic_body_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    cJSON *expected = parse(wire);
    if (anthropic_decode_response_text(wire, &decoded, &refusal) != SPISOR_OK ||
        anthropic_encode_response_of(decoded.response, &encoded, &refusal) != SPISOR_OK ||
        !anthropic_body_equals(&encoded, expected)) {
        support_fail("a response round trip", "did not restore the wire");
    }
    anthropic_definition()->release_body(NULL, &encoded);
    anthropic_definition()->release_response(NULL, &decoded);
    anthropic_definition()->release_refusal(NULL, &refusal);
    cJSON_Delete(expected);
}

static void a_response_body_has_the_bytes_the_translator_writes(void) {
    static const char expected[] =
        "{\"2\":1,\"id\":\"i\",\"type\":\"message\",\"role\":\"assistant\",\"content\":[],\"model\":\"m\","
        "\"stop_reason\":\"end_turn\",\"stop_sequence\":null,\"usage\":{\"input_tokens\":1},\"b\":0.5}";
    cJSON *ir = parse("{\"id\":\"i\",\"model\":\"m\",\"content\":[],\"stopReason\":\"end_turn\","
                      "\"usage\":{\"inputTokens\":1},\"extensions\":{\"b\":0.5,\"2\":1}}");
    ir_json_arena arena = {0};
    forebay_ir_response response;
    forebay_anthropic_body_answer answer = SPISOR_INIT(forebay_anthropic_body_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    if (ir == NULL || !anthropic_read_response_ir(ir, &arena, &response) ||
        anthropic_encode_response_of(&response, &answer, &refusal) != SPISOR_OK) {
        support_fail("the bytes of a response body", "the response was not encoded");
    } else if (answer.length != sizeof expected - 1 || memcmp(answer.bytes, expected, answer.length) != 0 ||
               answer.bytes[answer.length] != '\0') {
        support_failf("the bytes of a response body", "wrote %.*s", (int)answer.length, (const char *)answer.bytes);
    }
    anthropic_definition()->release_body(NULL, &answer);
    anthropic_definition()->release_refusal(NULL, &refusal);
    ir_json_arena_release(&arena);
    cJSON_Delete(ir);
}

static void expect_malformed_response(const char *label, const forebay_ir_response *response) {
    forebay_anthropic_body_answer answer = SPISOR_INIT(forebay_anthropic_body_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    spisor_status status = anthropic_encode_response_of(response, &answer, &refusal);
    if (status != SPISOR_MALFORMED || refusal.detail == NULL || refusal.fix == NULL || answer.storage != NULL ||
        answer.bytes != NULL) {
        support_failf(label, "answered %d, not MALFORMED with a refusal and an empty answer", (int)status);
    }
    anthropic_definition()->release_refusal(NULL, &refusal);
}

static forebay_ir_response valid_response(void) {
    forebay_ir_response response = SPISOR_INIT(forebay_ir_response);
    response.id = "i";
    response.model = "m";
    response.stop_reason = "end_turn";
    return response;
}

static void a_response_that_breaks_its_header_is_malformed(void) {
    static const forebay_ir_block text_without_text = {.kind = FOREBAY_IR_BLOCK_TEXT};
    static const spisor_value empty_text = {SPISOR_VALUE_TEXT, 0, 0, NULL, 0, NULL, NULL, 0};
    forebay_ir_response response = valid_response();
    response.size = (uint32_t)sizeof response.size;
    expect_malformed_response("a response short of its first shape", &response);
    expect_malformed_response("a NULL response", NULL);
    response = valid_response();
    response.id = NULL;
    expect_malformed_response("a response without an id", &response);
    response = valid_response();
    response.model = NULL;
    expect_malformed_response("a response without a model", &response);
    response = valid_response();
    response.stop_reason = NULL;
    expect_malformed_response("a response without a stop reason", &response);
    response = valid_response();
    response.content_count = 1;
    expect_malformed_response("content NULL beside a count", &response);
    response = valid_response();
    response.content = &text_without_text;
    response.content_count = 1;
    expect_malformed_response("a text block without text", &response);
    response = valid_response();
    response.extensions = &empty_text;
    expect_malformed_response("extensions text without bytes", &response);
}

static void run_shape_cases(void) {
    for (size_t index = 0; index < ANTHROPIC_SHAPE_CASE_COUNT; index++) {
        const anthropic_shape_case *shape = &ANTHROPIC_SHAPE_CASES[index];
        cJSON *input = parse(shape->input);
        cJSON *expected = parse(shape->expected);
        const anthropic_direction *direction = anthropic_direction_named(shape->direction);
        if (input == NULL || expected == NULL || direction == NULL) {
            support_fail(shape->label, "the case could not be read");
        } else {
            anthropic_check_vector_input(direction, shape->label, input, expected);
        }
        cJSON_Delete(input);
        cJSON_Delete(expected);
    }
}

static void expect_body_bytes(const char *label, spisor_status status, const forebay_anthropic_body_answer *answer,
                              const char *expected) {
    if (status != SPISOR_OK || answer->bytes == NULL) {
        support_failf(label, "answered %d", (int)status);
    } else if (answer->length != strlen(expected) || memcmp(answer->bytes, expected, answer->length) != 0 ||
               answer->bytes[answer->length] != '\0') {
        support_failf(label, "wrote %.*s", (int)answer->length, (const char *)answer->bytes);
    }
}

/** The bytes JSON.stringify writes: members in the order the proxy builds them, a control character escaped. */
static void the_shapes_have_the_bytes_the_proxy_writes(void) {
    static const char message[] = "a\"b\\c\x01 caf\xc3\xa9";
    static const char expected_error[] =
        "{\"type\":\"error\",\"error\":{\"type\":\"api_error\",\"message\":\"a\\\"b\\\\c\\u0001 caf\xc3\xa9\"}}";
    forebay_anthropic_error_argument error = SPISOR_INIT(forebay_anthropic_error_argument);
    error.error_type = "api_error";
    error.message = message;
    forebay_anthropic_body_answer answer = SPISOR_INIT(forebay_anthropic_body_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    spisor_status status = anthropic_definition()->error_envelope(NULL, &error, &answer, &refusal);
    expect_body_bytes("the bytes of an error body", status, &answer, expected_error);
    anthropic_definition()->release_body(NULL, &answer);

    static const forebay_anthropic_listed_model models[] = {
        {.id = "a", .name = "A", .has_context = 1, .context = 9, .has_output = 1, .output = 8}, {.id = "b"}};
    const forebay_anthropic_listed_provider provider = {.models = models, .model_count = 2};
    forebay_anthropic_model_listing_argument listing = SPISOR_INIT(forebay_anthropic_model_listing_argument);
    listing.default_context = 5;
    listing.default_output = 6;
    listing.providers = &provider;
    listing.provider_count = 1;
    answer = (forebay_anthropic_body_answer)SPISOR_INIT(forebay_anthropic_body_answer);
    status = anthropic_definition()->model_listing(NULL, &listing, &answer, &refusal);
    expect_body_bytes("the bytes of a listing body", status, &answer,
                      "{\"data\":[{\"type\":\"model\",\"id\":\"a\",\"display_name\":\"A\","
                      "\"created_at\":\"2025-01-01T00:00:00Z\",\"max_input_tokens\":9,\"max_tokens\":8},"
                      "{\"type\":\"model\",\"id\":\"b\",\"display_name\":\"b\","
                      "\"created_at\":\"2025-01-01T00:00:00Z\",\"max_input_tokens\":5,\"max_tokens\":6}],"
                      "\"first_id\":\"a\",\"last_id\":\"b\",\"has_more\":false}");
    anthropic_definition()->release_body(NULL, &answer);
    anthropic_definition()->release_refusal(NULL, &refusal);
}

static void expect_refused(const char *label, spisor_status status, const forebay_anthropic_body_answer *answer,
                           const spisor_refusal *refusal) {
    if (status != SPISOR_MALFORMED || refusal->detail == NULL || refusal->fix == NULL || answer->storage != NULL ||
        answer->bytes != NULL) {
        support_failf(label, "answered %d, not MALFORMED with a refusal and an empty answer", (int)status);
    }
}

static void expect_error_malformed(const char *label, const char *error_type, const char *message) {
    forebay_anthropic_error_argument error = SPISOR_INIT(forebay_anthropic_error_argument);
    error.error_type = error_type;
    error.message = message;
    forebay_anthropic_body_answer answer = SPISOR_INIT(forebay_anthropic_body_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    expect_refused(label, anthropic_definition()->error_envelope(NULL, &error, &answer, &refusal), &answer, &refusal);
    anthropic_definition()->release_refusal(NULL, &refusal);
}

static void an_error_call_that_breaks_its_header_is_malformed(void) {
    expect_error_malformed("a NULL error type", NULL, "m");
    expect_error_malformed("an empty error type", "", "m");
    expect_error_malformed("a NULL message", "api_error", NULL);
    forebay_anthropic_error_argument error = SPISOR_INIT(forebay_anthropic_error_argument);
    error.error_type = "api_error";
    error.message = "m";
    forebay_anthropic_body_answer answer = SPISOR_INIT(forebay_anthropic_body_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    expect_refused("a NULL error argument", anthropic_definition()->error_envelope(NULL, NULL, &answer, &refusal),
                   &answer, &refusal);
    anthropic_definition()->release_refusal(NULL, &refusal);
    error.size = (uint32_t)sizeof error.size;
    expect_refused("an error argument short of its first shape",
                   anthropic_definition()->error_envelope(NULL, &error, &answer, &refusal), &answer, &refusal);
    anthropic_definition()->release_refusal(NULL, &refusal);
    error.size = (uint32_t)sizeof error;
    int held = 0;
    answer.storage = &held;
    spisor_status status = anthropic_definition()->error_envelope(NULL, &error, &answer, &refusal);
    if (status != SPISOR_MALFORMED || refusal.detail == NULL) {
        support_failf("an error answer that is not empty", "answered %d, not MALFORMED", (int)status);
    }
    anthropic_definition()->release_refusal(NULL, &refusal);
}

static void expect_listing_malformed(const char *label, const forebay_anthropic_model_listing_argument *listing,
                                     const char *member) {
    forebay_anthropic_body_answer answer = SPISOR_INIT(forebay_anthropic_body_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    expect_refused(label, anthropic_definition()->model_listing(NULL, listing, &answer, &refusal), &answer, &refusal);
    if (member != NULL && refusal.detail != NULL && strstr(refusal.detail, member) == NULL) {
        support_failf(label, "the refusal does not name %s: %s", member, refusal.detail);
    }
    anthropic_definition()->release_refusal(NULL, &refusal);
}

static void a_listing_that_breaks_its_header_is_malformed(void) {
    static const forebay_anthropic_listed_model nameless = {.name = "n"};
    static const forebay_anthropic_listed_model fine = {.id = "a"};
    const forebay_anthropic_listed_provider no_models = {.models = NULL, .model_count = 1};
    const forebay_anthropic_listed_provider with_nameless = {.models = &nameless, .model_count = 1};
    const forebay_anthropic_listed_provider good = {.models = &fine, .model_count = 1};
    const forebay_anthropic_listed_provider providers[2] = {good, with_nameless};
    forebay_anthropic_model_listing_argument listing = SPISOR_INIT(forebay_anthropic_model_listing_argument);
    expect_listing_malformed("a NULL listing argument", NULL, NULL);
    listing.size = (uint32_t)sizeof listing.size;
    expect_listing_malformed("a listing argument short of its first shape", &listing, NULL);
    listing = (forebay_anthropic_model_listing_argument)SPISOR_INIT(forebay_anthropic_model_listing_argument);
    listing.provider_count = 1;
    expect_listing_malformed("providers NULL beside a count", &listing, "providers");
    listing.providers = &no_models;
    expect_listing_malformed("models NULL beside a count", &listing, "providers[0].models");
    listing.providers = providers;
    listing.provider_count = 2;
    expect_listing_malformed("a model without an id", &listing, "providers[1].models[0].id");
}

static void releasing_an_answer_never_filled_does_nothing(void) {
    forebay_anthropic_request_answer request_answer = {0};
    forebay_anthropic_response_answer response_answer = {0};
    forebay_anthropic_body_answer body_answer = {0};
    spisor_refusal refusal = {0};
    anthropic_definition()->release_request(NULL, &request_answer);
    anthropic_definition()->release_response(NULL, &response_answer);
    anthropic_definition()->release_body(NULL, &body_answer);
    anthropic_definition()->release_refusal(NULL, &refusal);
    anthropic_definition()->release_response(NULL, NULL);
    anthropic_definition()->release_request(NULL, NULL);
    anthropic_definition()->release_body(NULL, NULL);
}

int main(void) {
    spisor_json_use_library_allocator();
    int vectors = run_vectors("request-");
    if (vectors != REQUEST_VECTOR_COUNT) {
        support_failf("request vectors", "%d files, expected %d", vectors, REQUEST_VECTOR_COUNT);
    }
    int response_vectors = run_vectors("response-");
    if (response_vectors != RESPONSE_VECTOR_COUNT) {
        support_failf("response vectors", "%d files, expected %d", response_vectors, RESPONSE_VECTOR_COUNT);
    }
    run_decode_cases();
    a_root_that_is_no_object_decodes_to_the_empty_request();
    a_number_that_is_not_finite_is_written_as_null();
    bytes_that_are_no_json_are_malformed();
    a_string_holding_nul_is_refused_as_unreadable_json();
    bytes_may_be_null_only_beside_a_length_of_zero();
    run_encode_cases();
    a_body_has_the_bytes_the_translator_writes();
    an_ir_that_breaks_its_header_is_malformed();
    an_extension_tree_that_is_not_well_formed_is_malformed();
    run_response_decode_cases();
    a_response_root_that_is_no_object_decodes_to_the_empty_response();
    run_response_encode_cases();
    a_response_decoded_then_encoded_restores_the_wire();
    a_response_body_has_the_bytes_the_translator_writes();
    a_response_that_breaks_its_header_is_malformed();
    run_shape_cases();
    the_shapes_have_the_bytes_the_proxy_writes();
    an_error_call_that_breaks_its_header_is_malformed();
    a_listing_that_breaks_its_header_is_malformed();
    releasing_an_answer_never_filled_does_nothing();
    printf("anthropic_test: %d request vectors, %d response vectors\n", vectors, response_vectors);
    return support_summary("anthropic_test");
}
