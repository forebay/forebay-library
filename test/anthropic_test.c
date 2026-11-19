#include "anthropic_call.h"

#include "spisor/json_allocation.h"

/**
 * @file
 * The anthropic service's request translation through its definition: every `request-*` vector in both directions (a
 * decode vector's `input` as bytes against its `output`, an encode vector's `input` against its `output`), then the
 * cases no vector writes: wire members of the wrong type, bytes that are not JSON, a root that is not an object, an IR
 * that breaks its header, and the byte form of a body.
 */

#define REQUEST_VECTOR_COUNT 18

static cJSON *parse(const char *text) {
    int out_of_memory = 0;
    return spisor_parse_json_reporting((const uint8_t *)text, strlen(text), &out_of_memory);
}

static void expect_equal_request(const forebay_anthropic_request_answer *answer, const cJSON *expected,
                                 const char *label) {
    if (answer->request == NULL || !anthropic_request_equals(answer->request, expected)) {
        support_failf(label, "the decoded request is not the expected one");
    }
}

static void check_decode(const char *label, const char *body, const cJSON *expected) {
    forebay_anthropic_request_answer answer = SPISOR_INIT(forebay_anthropic_request_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    spisor_status status = anthropic_decode_text(body, &answer, &refusal);
    if (status != SPISOR_OK) {
        support_failf(label, "decode answered %d", (int)status);
    } else {
        expect_equal_request(&answer, expected, label);
    }
    anthropic_definition()->release_request(NULL, &answer);
    anthropic_definition()->release_refusal(NULL, &refusal);
}

static void check_encode(const char *label, const cJSON *ir, const cJSON *expected) {
    ir_json_arena arena = {0};
    forebay_ir_request request;
    forebay_anthropic_body_answer answer = SPISOR_INIT(forebay_anthropic_body_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    if (!anthropic_read_ir(ir, &arena, &request)) {
        support_fail(label, "the IR is not one the test codec reads");
    } else if (anthropic_encode_request_of(&request, &answer, &refusal) != SPISOR_OK) {
        support_fail(label, "encode did not answer OK");
    } else if (!anthropic_body_equals(&answer, expected)) {
        support_fail(label, "the encoded body is not the expected one");
    }
    anthropic_definition()->release_body(NULL, &answer);
    anthropic_definition()->release_refusal(NULL, &refusal);
    ir_json_arena_release(&arena);
}

static void run_vector(const char *path) {
    cJSON *vector = spisor_vector_read(path);
    const cJSON *input = cJSON_GetObjectItemCaseSensitive(vector, "input");
    const cJSON *output = cJSON_GetObjectItemCaseSensitive(vector, "output");
    if (vector == NULL || input == NULL || output == NULL) {
        support_fail(path, "not a vector with an input and an output");
    } else if (anthropic_is_direction(vector, "decodeRequest")) {
        char *body = cJSON_PrintUnformatted(input);
        check_decode(path, body, output);
        cJSON_free(body);
    } else if (anthropic_is_direction(vector, "encodeRequest")) {
        check_encode(path, input, output);
    } else {
        support_fail(path, "a request vector of neither direction");
    }
    cJSON_Delete(vector);
}

static int run_request_vectors(void) {
    char *list = support_read_text(ANTHROPIC_VECTOR_LIST);
    int count = 0;
    if (list == NULL) {
        support_fail("vector list", ANTHROPIC_VECTOR_LIST);
        return 0;
    }
    for (char *line = strtok(list, "\r\n"); line != NULL; line = strtok(NULL, "\r\n")) {
        if (strstr(line, "request-") != NULL) {
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
            check_decode(DECODE_CASES[index].label, DECODE_CASES[index].body, expected);
        }
        cJSON_Delete(expected);
    }
}

static void a_root_that_is_no_object_decodes_to_the_empty_request(void) {
    static const char *const roots[] = {"[]", "5", "\"x\"", "null", "true", "[{\"model\":\"m\"}]"};
    cJSON *empty = parse("{\"model\":\"\",\"messages\":[],\"stream\":false}");
    for (size_t index = 0; index < sizeof roots / sizeof roots[0]; index++) {
        check_decode(roots[index], roots[index], empty);
    }
    cJSON_Delete(empty);
}

static void bytes_that_are_no_json_are_malformed(void) {
    static const char *const bodies[] = {"not json", "", "{", "[1,2", "{\"a\":1} x", "\xff\xfe", "{\"a\":\"\\u0000\"}"};
    for (size_t index = 0; index < sizeof bodies / sizeof bodies[0]; index++) {
        forebay_anthropic_request_answer answer = SPISOR_INIT(forebay_anthropic_request_answer);
        spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
        spisor_status status = anthropic_decode_text(bodies[index], &answer, &refusal);
        if (status != SPISOR_MALFORMED || refusal.detail == NULL || refusal.fix == NULL ||
            strstr(refusal.fix, "JSON") == NULL || answer.request != NULL || answer.storage != NULL) {
            support_failf("bytes that are no JSON", "\"%s\" answered %d", bodies[index], (int)status);
        }
        anthropic_definition()->release_refusal(NULL, &refusal);
    }
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
            check_encode(ENCODE_CASES[index].label, ir, expected);
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
    static const forebay_ir_block unnamed_kind = {99};
    static const forebay_ir_block unknown_without_raw = {FOREBAY_IR_BLOCK_UNKNOWN};
    static const forebay_ir_block text_without_text = {FOREBAY_IR_BLOCK_TEXT};
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

static void releasing_an_answer_never_filled_does_nothing(void) {
    forebay_anthropic_request_answer request_answer = {0};
    forebay_anthropic_body_answer body_answer = {0};
    spisor_refusal refusal = {0};
    anthropic_definition()->release_request(NULL, &request_answer);
    anthropic_definition()->release_body(NULL, &body_answer);
    anthropic_definition()->release_refusal(NULL, &refusal);
    anthropic_definition()->release_request(NULL, NULL);
    anthropic_definition()->release_body(NULL, NULL);
}

int main(void) {
    spisor_json_use_library_allocator();
    int vectors = run_request_vectors();
    if (vectors != REQUEST_VECTOR_COUNT) {
        support_failf("request vectors", "%d files, expected %d", vectors, REQUEST_VECTOR_COUNT);
    }
    run_decode_cases();
    a_root_that_is_no_object_decodes_to_the_empty_request();
    bytes_that_are_no_json_are_malformed();
    bytes_may_be_null_only_beside_a_length_of_zero();
    run_encode_cases();
    a_body_has_the_bytes_the_translator_writes();
    an_ir_that_breaks_its_header_is_malformed();
    releasing_an_answer_never_filled_does_nothing();
    printf("anthropic_test: %d request vectors\n", vectors);
    return support_summary("anthropic_test");
}
