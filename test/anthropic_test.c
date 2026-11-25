#include "anthropic_call.h"
#include "anthropic_shape_cases.h"

#include <math.h>

#include "spisor/definition.h"
#include "spisor/json_allocation.h"

/**
 * @file
 * The anthropic service's translation through its definition: every `request-*` and `response-*` vector in both
 * directions (a decode vector's `input` as bytes against its `output`, an encode vector's `input` against its
 * `output`) and every `rate-limit-*`, `reset-time-*`, `decode-*`, `route-*` and `envelope-*` one, then the cases no
 * vector writes: wire members of the wrong type, bytes that are not JSON, a root that is not an object, an IR that
 * breaks its header, and the byte form of a body.
 */

#define REQUEST_VECTOR_COUNT 18
#define RESPONSE_VECTOR_COUNT 16
#define RATE_LIMIT_VECTOR_COUNT 21
#define DECODE_VECTOR_COUNT 41
#define ROUTE_VECTOR_COUNT 20
#define ERROR_ENVELOPE_VECTOR_COUNT 7
#define REPLY_VECTOR_COUNT 7
#define RESET_TIME_VECTOR_COUNT 20
#define PAST_TWO_TO_THE_53 9007199254740994.0

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

/** The reply a body goes out in, as an answer of any kind holds it. */
typedef struct reply_head {
    uint32_t status;
    const char *content_type;
    const forebay_pair *headers;
    size_t header_count;
} reply_head;

static reply_head head_of_body(const forebay_anthropic_body_answer *body) {
    return (reply_head){body->status, body->content_type, body->headers, body->header_count};
}

static int is_head(const reply_head *head, const cJSON *expected) {
    const cJSON *headers = cJSON_GetObjectItemCaseSensitive(expected, "headers");
    const char *content_type = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(expected, "contentType"));
    int same = head->status == (uint32_t)cJSON_GetObjectItemCaseSensitive(expected, "status")->valuedouble &&
               head->content_type != NULL && content_type != NULL && strcmp(head->content_type, content_type) == 0 &&
               head->header_count == (size_t)cJSON_GetArraySize(headers);
    size_t index = 0;
    const cJSON *header = NULL;
    cJSON_ArrayForEach(header, headers) {
        same = same && index < head->header_count && strcmp(head->headers[index].name, header->string) == 0 &&
               strcmp(head->headers[index].value, cJSON_GetStringValue(header)) == 0;
        index++;
    }
    return same;
}

/** Whether every answer the direction gave carries the reply `expected`: a stream's every feed, else its one body. */
static int answers_the_head(const anthropic_direction *direction, const anthropic_answer *answer,
                            const cJSON *expected) {
    if (strcmp(direction->name, "modelLookup") == 0) {
        const forebay_anthropic_model_lookup_answer *lookup = &answer->lookup;
        reply_head head = {lookup->status, lookup->content_type, lookup->headers, lookup->header_count};
        return is_head(&head, expected);
    }
    if (strcmp(direction->name, "encodeStream") != 0) {
        reply_head head = head_of_body(&answer->body);
        return is_head(&head, expected);
    }
    int every = answer->stream.count > 0;
    for (size_t index = 0; index < answer->stream.count; index++) {
        reply_head head = head_of_body(&answer->stream.texts[index]);
        every = every && is_head(&head, expected);
    }
    return every;
}

/** A reply vector: its direction's call answers the `output`, and the `reply` the output goes out in. */
static void run_reply_vector(const char *path) {
    cJSON *vector = spisor_vector_read(path);
    const cJSON *input_json = cJSON_GetObjectItemCaseSensitive(vector, "input");
    const cJSON *output = cJSON_GetObjectItemCaseSensitive(vector, "output");
    const cJSON *reply = cJSON_GetObjectItemCaseSensitive(vector, "reply");
    const anthropic_direction *direction = anthropic_direction_of(vector);
    anthropic_input input = {0};
    if (input_json == NULL || output == NULL || reply == NULL || direction == NULL ||
        !direction->read_input(input_json, &input)) {
        support_fail(path, "not a reply vector with an input, an output, a reply and a direction");
        anthropic_input_release(&input);
        cJSON_Delete(vector);
        return;
    }
    anthropic_answer answer;
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    anthropic_start_answer(direction, &answer);
    spisor_status status = direction->call(&input, &answer, &refusal);
    if (status != SPISOR_OK || !direction->matches(&answer, output)) {
        support_failf(path, "%s answered %d, or not the output", direction->name, (int)status);
    } else if (!answers_the_head(direction, &answer, reply)) {
        support_failf(path, "%s did not answer the reply", direction->name);
    }
    direction->release(&answer);
    anthropic_definition()->release_refusal(NULL, &refusal);
    anthropic_input_release(&input);
    cJSON_Delete(vector);
}

static int run_reply_vectors(void) {
    char *list = support_read_text(ANTHROPIC_VECTOR_LIST);
    int count = 0;
    if (list == NULL) {
        support_fail("vector list", ANTHROPIC_VECTOR_LIST);
        return 0;
    }
    for (char *line = strtok(list, "\r\n"); line != NULL; line = strtok(NULL, "\r\n")) {
        if (strstr(line, "/reply-") != NULL) {
            run_reply_vector(line);
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

static const char *const NON_OBJECT_ROOTS[] = {"[]", "5", "\"x\"", "null", "true", "[{\"model\":\"m\"}]"};

static void a_root_that_is_no_object_is_answered_as_an_invalid_request(void) {
    for (size_t index = 0; index < sizeof NON_OBJECT_ROOTS / sizeof NON_OBJECT_ROOTS[0]; index++) {
        forebay_anthropic_request_answer answer = SPISOR_INIT(forebay_anthropic_request_answer);
        spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
        spisor_status status = anthropic_decode_text(NON_OBJECT_ROOTS[index], &answer, &refusal);
        if (status != SPISOR_OK || answer.request != NULL || answer.storage != NULL || answer.forwarded_header_count != 0 ||
            answer.reply_kind != FOREBAY_REPLY_INVALID_REQUEST || answer.status != 400 || answer.message == NULL) {
            support_failf("a root that is no object", "%s answered %d with reply kind %u", NON_OBJECT_ROOTS[index],
                          (int)status, (unsigned)answer.reply_kind);
        }
        anthropic_definition()->release_request(NULL, &answer);
        anthropic_definition()->release_refusal(NULL, &refusal);
    }
}

static void an_answer_that_cannot_hold_the_reply_is_refused_a_root_that_is_no_object(void) {
    forebay_anthropic_request_answer answer = SPISOR_INIT(forebay_anthropic_request_answer);
    answer.size = (uint32_t)SPISOR_SHAPE_THROUGH(forebay_anthropic_request_answer, storage);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    spisor_status status = anthropic_decode_text("[]", &answer, &refusal);
    if (status != SPISOR_MALFORMED || refusal.detail == NULL || answer.request != NULL || answer.storage != NULL) {
        support_failf("a first-shape answer", "answered %d for a root that is no object", (int)status);
    }
    anthropic_definition()->release_refusal(NULL, &refusal);
}

static void an_answer_of_the_first_shape_gets_the_request_and_no_forwarded_headers(void) {
    static const forebay_pair headers[] = {{"Anthropic-Beta", "b"}};
    static const char body[] = "{\"model\":\"m\"}";
    forebay_anthropic_body_argument argument = SPISOR_INIT(forebay_anthropic_body_argument);
    argument.bytes = (const uint8_t *)body;
    argument.length = sizeof body - 1;
    argument.headers = headers;
    argument.header_count = 1;
    forebay_anthropic_request_answer answer = SPISOR_INIT(forebay_anthropic_request_answer);
    answer.size = (uint32_t)SPISOR_SHAPE_THROUGH(forebay_anthropic_request_answer, storage);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    spisor_status status = anthropic_definition()->decode_request(NULL, &argument, &answer, &refusal);
    if (status != SPISOR_OK || answer.request == NULL || answer.forwarded_headers != NULL ||
        answer.forwarded_header_count != 0) {
        support_failf("a first-shape answer", "answered %d, with forwarded headers beyond its size", (int)status);
    }
    anthropic_definition()->release_request(NULL, &answer);
    anthropic_definition()->release_refusal(NULL, &refusal);
}

static void expect_decode_malformed(const char *label, const forebay_anthropic_body_argument *argument,
                                    forebay_anthropic_request_answer *answer) {
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    spisor_status status = anthropic_definition()->decode_request(NULL, argument, answer, &refusal);
    if (status != SPISOR_MALFORMED || answer->request != NULL || answer->storage != NULL) {
        support_failf(label, "answered %d, not an empty MALFORMED", (int)status);
    }
    anthropic_definition()->release_refusal(NULL, &refusal);
}

static void a_decode_that_breaks_its_header_is_malformed(void) {
    static const char body[] = "{\"model\":\"m\"}";
    static const forebay_pair nameless[] = {{NULL, "v"}};
    static const forebay_pair valueless[] = {{"n", NULL}};
    forebay_anthropic_body_argument argument = SPISOR_INIT(forebay_anthropic_body_argument);
    argument.bytes = (const uint8_t *)body;
    argument.length = sizeof body - 1;
    forebay_anthropic_request_answer answer = SPISOR_INIT(forebay_anthropic_request_answer);
    argument.header_count = 1;
    expect_decode_malformed("a header count beside NULL headers", &argument, &answer);
    argument.headers = nameless;
    expect_decode_malformed("a header without a name", &argument, &answer);
    argument.headers = valueless;
    expect_decode_malformed("a header without a value", &argument, &answer);
    argument.header_count = 0;
    argument.headers = NULL;
    answer.forwarded_header_count = 1;
    expect_decode_malformed("forwarded headers counted beside no storage", &argument, &answer);
}

static void a_route_call_that_breaks_its_header_is_malformed(void) {
    const forebay_anthropic_route_argument valid = {(uint32_t)sizeof(forebay_anthropic_route_argument), "GET", "/"};
    const struct {
        const char *label;
        forebay_anthropic_route_argument argument;
        uint32_t answer_size;
    } cases[] = {{"a NULL method", {valid.size, NULL, "/"}, valid.size},
                 {"an empty method", {valid.size, "", "/"}, valid.size},
                 {"a NULL path", {valid.size, "GET", NULL}, valid.size},
                 {"an argument short of its first shape", {(uint32_t)sizeof(uint32_t), "GET", "/"}, valid.size},
                 {"an answer short of its first shape", valid, (uint32_t)sizeof(uint32_t)}};
    for (size_t index = 0; index < sizeof cases / sizeof cases[0]; index++) {
        forebay_anthropic_route_answer answer = SPISOR_INIT(forebay_anthropic_route_answer);
        answer.size = cases[index].answer_size;
        spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
        spisor_status status = anthropic_definition()->route_request(NULL, &cases[index].argument, &answer, &refusal);
        if (status != SPISOR_MALFORMED || answer.kind != 0 || answer.model_id != NULL || answer.storage != NULL) {
            support_failf(cases[index].label, "answered %d, not an empty MALFORMED", (int)status);
        }
        anthropic_definition()->release_route(NULL, &answer);
        anthropic_definition()->release_refusal(NULL, &refusal);
    }
    anthropic_definition()->release_route(NULL, NULL);
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

static void expect_unreadable_reply(const char *body, spisor_status status, const forebay_anthropic_request_answer *answer) {
    if (status != SPISOR_OK || answer->request != NULL || answer->storage != NULL ||
        answer->reply_kind != FOREBAY_REPLY_INVALID_REQUEST || answer->status != 400 || answer->message == NULL ||
        strstr(answer->message, "not JSON") == NULL) {
        support_failf("a request body that is no JSON", "\"%s\" answered %d, not the invalid-request reply", body,
                      (int)status);
    }
}

static void bytes_that_are_no_json_are_malformed_to_a_response_and_invalid_to_a_request(void) {
    for (size_t index = 0; index < sizeof NON_JSON_BODIES / sizeof NON_JSON_BODIES[0]; index++) {
        forebay_anthropic_request_answer answer = SPISOR_INIT(forebay_anthropic_request_answer);
        spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
        spisor_status status = anthropic_decode_text(NON_JSON_BODIES[index], &answer, &refusal);
        expect_unreadable_reply(NON_JSON_BODIES[index], status, &answer);
        anthropic_definition()->release_request(NULL, &answer);
        anthropic_definition()->release_refusal(NULL, &refusal);
        forebay_anthropic_response_answer response_answer = SPISOR_INIT(forebay_anthropic_response_answer);
        status = anthropic_decode_response_text(NON_JSON_BODIES[index], &response_answer, &refusal);
        expect_not_json_refusal(NON_JSON_BODIES[index], status, &refusal, response_answer.storage);
        anthropic_definition()->release_refusal(NULL, &refusal);
    }
}

static void a_string_holding_nul_is_refused_as_unreadable_json(void) {
    static const char body[] = "{\"a\":\"\\u0000\"}";
    forebay_anthropic_request_answer answer = SPISOR_INIT(forebay_anthropic_request_answer);
    forebay_anthropic_response_answer response_answer = SPISOR_INIT(forebay_anthropic_response_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    expect_unreadable_reply(body, anthropic_decode_text(body, &answer, &refusal), &answer);
    spisor_status status = anthropic_decode_response_text(body, &response_answer, &refusal);
    if (status != SPISOR_MALFORMED || refusal.detail == NULL || strstr(refusal.detail, "NUL") == NULL) {
        support_failf("a string holding NUL", "answered %d without saying NUL", (int)status);
    }
    anthropic_definition()->release_request(NULL, &answer);
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
    expect_unreadable_reply("NULL bytes and no length", status, &answer);
    anthropic_definition()->release_request(NULL, &answer);
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

/** `extensions` as a block's, a tool's and a tool choice's own, each in a request otherwise valid. */
static void expect_malformed_in_a_block_a_tool_and_a_tool_choice(const char *label, const spisor_value *extensions) {
    static const spisor_value empty_record = {SPISOR_VALUE_RECORD, 0, 0, NULL, 0, NULL, NULL, 0};
    char where[96];
    const forebay_ir_block block = {.kind = FOREBAY_IR_BLOCK_TEXT, .text = "t", .extensions = extensions};
    const forebay_ir_message message = {"user", &block, 1, NULL};
    forebay_ir_request request = valid_request();
    request.messages = &message;
    request.message_count = 1;
    snprintf(where, sizeof where, "%s, on a block", label);
    expect_malformed(where, &request);
    const forebay_ir_tool tool = {"n", NULL, &empty_record, extensions};
    request = valid_request();
    request.has_tools = 1;
    request.tools = &tool;
    request.tool_count = 1;
    snprintf(where, sizeof where, "%s, on a tool", label);
    expect_malformed(where, &request);
    const forebay_ir_tool_choice choice = {"auto", NULL, extensions};
    request = valid_request();
    request.tool_choice = &choice;
    snprintf(where, sizeof where, "%s, on a tool choice", label);
    expect_malformed(where, &request);
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
        expect_malformed_in_a_block_a_tool_and_a_tool_choice(names[index], bad[index]);
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
    {"an empty object keeps all four raw members, though the wire gives room for none",
     "{}",
     "{\"id\":\"\",\"model\":\"\",\"content\":[],\"stopReason\":\"error\",\"extensions\":{\"$stopReasonRaw\":null,"
     "\"$stopSequenceRaw\":null,\"$typeRaw\":null,\"$roleRaw\":null}}"},
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
    error.reply_kind = FOREBAY_REPLY_API_ERROR;
    error.message = message;
    forebay_anthropic_body_answer answer = SPISOR_INIT(forebay_anthropic_body_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    spisor_status status = anthropic_definition()->error_envelope(NULL, &error, &answer, &refusal);
    expect_body_bytes("the bytes of an error body", status, &answer, expected_error);
    anthropic_definition()->release_body(NULL, &answer);

    static const forebay_catalog_model models[] = {
        {.id = "a", .name = "A", .has_context = 1, .context = 9, .has_output = 1, .output = 8},
        {.id = "b", .name = "b", .has_context = 1, .context = 5, .has_output = 1, .output = 6}};
    forebay_anthropic_model_listing_argument listing = SPISOR_INIT(forebay_anthropic_model_listing_argument);
    listing.models = models;
    listing.model_count = 2;
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

static void expect_error_malformed(const char *label, uint32_t reply_kind, const char *message) {
    forebay_anthropic_error_argument error = SPISOR_INIT(forebay_anthropic_error_argument);
    error.reply_kind = reply_kind;
    error.message = message;
    forebay_anthropic_body_answer answer = SPISOR_INIT(forebay_anthropic_body_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    expect_refused(label, anthropic_definition()->error_envelope(NULL, &error, &answer, &refusal), &answer, &refusal);
    anthropic_definition()->release_refusal(NULL, &refusal);
}

static void an_error_call_that_breaks_its_header_is_malformed(void) {
    expect_error_malformed("no reply kind", 0, "m");
    expect_error_malformed("a reply kind reply.h does not name", FOREBAY_REPLY_INVALID_REQUEST + 1, "m");
    expect_error_malformed("a NULL message", FOREBAY_REPLY_API_ERROR, NULL);
    forebay_anthropic_error_argument error = SPISOR_INIT(forebay_anthropic_error_argument);
    error.reply_kind = FOREBAY_REPLY_API_ERROR;
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
    static const forebay_catalog_model without_id[] = {{.id = "a", .name = "A"}, {.name = "n"}};
    static const forebay_catalog_model without_name[] = {{.id = "a"}};
    forebay_anthropic_model_listing_argument listing = SPISOR_INIT(forebay_anthropic_model_listing_argument);
    expect_listing_malformed("a NULL listing argument", NULL, NULL);
    listing.size = (uint32_t)sizeof listing.size;
    expect_listing_malformed("a listing argument short of its first shape", &listing, NULL);
    listing = (forebay_anthropic_model_listing_argument)SPISOR_INIT(forebay_anthropic_model_listing_argument);
    listing.model_count = 1;
    expect_listing_malformed("models NULL beside a count", &listing, "models");
    listing.models = without_id;
    listing.model_count = 2;
    expect_listing_malformed("an entry without an id", &listing, "models[1].id");
    listing.models = without_name;
    listing.model_count = 1;
    expect_listing_malformed("an entry without a name", &listing, "models[0].name");
}

static void expect_lookup(const char *label, const char *id, uint32_t found, const char *expected_bytes) {
    static const forebay_catalog_model models[] = {
        {.id = "a", .name = "A", .has_context = 1, .context = 5, .has_output = 1, .output = 6},
        {.id = "b", .name = "b", .has_context = 2, .context = 7, .has_output = 3, .output = 8},
        {.id = "a", .name = "A later", .has_context = 1, .context = 1, .has_output = 1, .output = 1}};
    forebay_anthropic_model_lookup_argument argument = SPISOR_INIT(forebay_anthropic_model_lookup_argument);
    argument.models = models;
    argument.model_count = 3;
    argument.id = id;
    forebay_anthropic_model_lookup_answer answer = SPISOR_INIT(forebay_anthropic_model_lookup_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    spisor_status status = anthropic_definition()->model_lookup(NULL, &argument, &answer, &refusal);
    size_t expected_length = strlen(expected_bytes);
    if (status != SPISOR_OK || answer.found != found || answer.length != expected_length ||
        memcmp(answer.bytes, expected_bytes, expected_length) != 0 || answer.bytes[answer.length] != '\0') {
        support_failf(label, "answered %d, found %d, wrote %.*s", (int)status, (int)answer.found, (int)answer.length,
                      (const char *)answer.bytes);
    }
    anthropic_definition()->release_model_lookup(NULL, &answer);
    anthropic_definition()->release_refusal(NULL, &refusal);
}

static void a_lookup_answers_found_or_not_found_with_the_proxys_bodies(void) {
    expect_lookup("a found model", "a", 1,
                  "{\"type\":\"model\",\"id\":\"a\",\"display_name\":\"A\",\"created_at\":\"2025-01-01T00:00:00Z\","
                  "\"max_input_tokens\":5,\"max_tokens\":6}");
    expect_lookup("limits whose has_ flags are set but not 1", "b", 1,
                  "{\"type\":\"model\",\"id\":\"b\",\"display_name\":\"b\",\"created_at\":\"2025-01-01T00:00:00Z\","
                  "\"max_input_tokens\":7,\"max_tokens\":8}");
    expect_lookup("a model not found", "zz", 0,
                  "{\"type\":\"error\",\"error\":{\"type\":\"not_found_error\",\"message\":\"model not found: zz\"}}");
}

static void expect_lookup_malformed(const char *label, const forebay_anthropic_model_lookup_argument *argument,
                                    const char *member) {
    forebay_anthropic_model_lookup_answer answer = SPISOR_INIT(forebay_anthropic_model_lookup_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    spisor_status status = anthropic_definition()->model_lookup(NULL, argument, &answer, &refusal);
    if (status != SPISOR_MALFORMED || refusal.detail == NULL || refusal.fix == NULL || answer.storage != NULL ||
        answer.bytes != NULL) {
        support_failf(label, "answered %d, not MALFORMED with a refusal and an empty answer", (int)status);
    } else if (member != NULL && strstr(refusal.detail, member) == NULL) {
        support_failf(label, "the refusal does not name %s: %s", member, refusal.detail);
    }
    anthropic_definition()->release_refusal(NULL, &refusal);
}

static void a_lookup_that_breaks_its_header_is_malformed(void) {
    forebay_anthropic_model_lookup_argument argument = SPISOR_INIT(forebay_anthropic_model_lookup_argument);
    expect_lookup_malformed("a NULL lookup argument", NULL, NULL);
    expect_lookup_malformed("a lookup without an id", &argument, "id");
    argument.id = "a";
    argument.model_count = 1;
    expect_lookup_malformed("models NULL beside a count", &argument, "models");
    argument.size = (uint32_t)sizeof argument.size;
    expect_lookup_malformed("a lookup argument short of its first shape", &argument, NULL);
    anthropic_definition()->release_model_lookup(NULL, NULL);
}

static const char *const REQUIRED_RATE_LIMIT_VECTORS[] = {
    "rate-limit-upstream-429-reconciles-reset-and-strips-headers.json",
    "rate-limit-no-upstream-falls-back-to-sixty-seconds.json",
    "rate-limit-reset-header-5h-only.json",
    "rate-limit-reset-header-unified-only.json",
    "rate-limit-reset-header-none.json",
    "rate-limit-retry-after-alone-gives-the-reset.json",
};

/** The vectors of the reply's two reference cases and each reset header stay in the list. */
static void the_rate_limit_vectors_cover_the_required_cases(void) {
    char *list = support_read_text(ANTHROPIC_VECTOR_LIST);
    if (list == NULL) {
        support_fail("vector list", ANTHROPIC_VECTOR_LIST);
        return;
    }
    for (size_t index = 0; index < sizeof REQUIRED_RATE_LIMIT_VECTORS / sizeof REQUIRED_RATE_LIMIT_VECTORS[0]; index++) {
        if (strstr(list, REQUIRED_RATE_LIMIT_VECTORS[index]) == NULL) {
            support_fail(REQUIRED_RATE_LIMIT_VECTORS[index], "a rate-limit vector the reply must keep is missing");
        }
    }
    free(list);
}

static forebay_anthropic_rate_limit_argument valid_rate_limit(void) {
    forebay_anthropic_rate_limit_argument argument = SPISOR_INIT(forebay_anthropic_rate_limit_argument);
    argument.now = 1000;
    argument.zone_en_us = "UTC";
    argument.zone_en_gb = "UTC";
    return argument;
}

static void expect_rate_limit_malformed(const char *label, const forebay_anthropic_rate_limit_argument *argument,
                                        uint32_t answer_size) {
    forebay_anthropic_reply_answer answer = SPISOR_INIT(forebay_anthropic_reply_answer);
    answer.size = answer_size;
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    spisor_status status = anthropic_definition()->rate_limit_reply(NULL, argument, &answer, &refusal);
    if (status != SPISOR_MALFORMED || answer.headers != NULL || answer.body != NULL || answer.storage != NULL) {
        support_failf(label, "answered %d, not an empty MALFORMED", (int)status);
    }
    anthropic_definition()->release_reply(NULL, &answer);
    anthropic_definition()->release_refusal(NULL, &refusal);
}

/** A caller compiled before `zone_en_gb` was appended passes one name, which the reply uses as it is given. */
static void an_argument_short_of_the_en_gb_name_uses_the_en_us_one(void) {
    forebay_anthropic_rate_limit_argument argument = valid_rate_limit();
    forebay_anthropic_reply_answer answer = SPISOR_INIT(forebay_anthropic_reply_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    argument.size = (uint32_t)offsetof(forebay_anthropic_rate_limit_argument, zone_en_gb);
    argument.has_reset_at = 1;
    argument.reset_at = 1000000300000;
    argument.now = 1000000000000;
    argument.offset_minutes = 120;
    argument.zone_en_us = "GMT+2";
    argument.zone_en_gb = "CEST";
    spisor_status status = anthropic_definition()->rate_limit_reply(NULL, &argument, &answer, &refusal);
    if (status != SPISOR_OK || answer.body == NULL || strstr(answer.body, "resets at 3:51 AM GMT+2\"") == NULL) {
        support_failf("an argument short of zone_en_gb", "answered %d with %s", (int)status,
                      answer.body == NULL ? "no body" : answer.body);
    }
    anthropic_definition()->release_reply(NULL, &answer);
    anthropic_definition()->release_refusal(NULL, &refusal);
}

static void a_rate_limit_call_that_breaks_its_header_is_malformed(void) {
    const uint32_t answer_size = (uint32_t)sizeof(forebay_anthropic_reply_answer);
    forebay_anthropic_rate_limit_argument argument = valid_rate_limit();
    expect_rate_limit_malformed("a NULL argument", NULL, answer_size);
    expect_rate_limit_malformed("an answer short of its first shape", &argument, (uint32_t)sizeof(uint32_t));
    argument.size = (uint32_t)sizeof argument.size;
    expect_rate_limit_malformed("an argument short of its first shape", &argument, answer_size);
    argument = valid_rate_limit();
    argument.zone_en_us = NULL;
    expect_rate_limit_malformed("a NULL en-US zone", &argument, answer_size);
    argument.zone_en_us = "";
    expect_rate_limit_malformed("an empty en-US zone", &argument, answer_size);
    argument = valid_rate_limit();
    argument.zone_en_gb = NULL;
    expect_rate_limit_malformed("a NULL en-GB zone", &argument, answer_size);
    argument.zone_en_gb = "";
    expect_rate_limit_malformed("an empty en-GB zone", &argument, answer_size);
    argument = valid_rate_limit();
    argument.upstream_header_count = 1;
    expect_rate_limit_malformed("a header count beside NULL headers", &argument, answer_size);
    static const forebay_pair nameless[] = {{NULL, "v"}};
    argument.upstream_headers = nameless;
    expect_rate_limit_malformed("a header without a name", &argument, answer_size);
    argument = valid_rate_limit();
    argument.has_reset_at = 1;
    argument.reset_at = INFINITY;
    expect_rate_limit_malformed("a reset that is not finite", &argument, answer_size);
    argument = valid_rate_limit();
    argument.now = NAN;
    expect_rate_limit_malformed("a now that is not a number", &argument, answer_size);
    argument = valid_rate_limit();
    argument.offset_minutes = 1441;
    expect_rate_limit_malformed("an offset of more than a day", &argument, answer_size);
}

/** The clock reads the reset plus the offset, past midnight in either direction and on the half hour. */
static void a_rate_limit_clock_follows_the_offset(void) {
    static const struct {
        double reset_at;
        int64_t offset_minutes;
        const char *time;
    } cases[] = {{86400000.0 * 3 + 600000, -660, "1:10 PM"},
                 {86400000.0 * 3 + 600000, 345, "5:55 AM"},
                 {43200000.0, 0, "12:00 PM"},
                 {60000.0, -2, "11:59 PM"}};
    for (size_t index = 0; index < sizeof cases / sizeof cases[0]; index++) {
        forebay_anthropic_rate_limit_argument argument = valid_rate_limit();
        argument.has_reset_at = 1;
        argument.reset_at = cases[index].reset_at;
        argument.offset_minutes = cases[index].offset_minutes;
        argument.now = 0;
        forebay_anthropic_reply_answer answer = SPISOR_INIT(forebay_anthropic_reply_answer);
        spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
        spisor_status status = anthropic_definition()->rate_limit_reply(NULL, &argument, &answer, &refusal);
        char wanted[64];
        snprintf(wanted, sizeof wanted, "resets at %s UTC", cases[index].time);
        if (status != SPISOR_OK || strstr(answer.body, wanted) == NULL) {
            support_failf("a rate-limit clock", "%s, answered %d: %s", wanted, (int)status,
                          answer.body == NULL ? "no body" : answer.body);
        }
        anthropic_definition()->release_reply(NULL, &answer);
        anthropic_definition()->release_refusal(NULL, &refusal);
    }
}

static void expect_reset_malformed(const char *label, const forebay_anthropic_reset_argument *argument,
                                   uint32_t answer_size, const char *member) {
    forebay_anthropic_reset_answer answer = SPISOR_INIT(forebay_anthropic_reset_answer);
    answer.size = answer_size;
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    spisor_status status = anthropic_definition()->reset_time(NULL, argument, &answer, &refusal);
    if (status != SPISOR_MALFORMED || refusal.detail == NULL || answer.has_reset_at != 0) {
        support_failf(label, "answered %d, not MALFORMED with a refusal and no reset", (int)status);
    } else if (member != NULL && strstr(refusal.detail, member) == NULL) {
        support_failf(label, "the refusal does not name %s: %s", member, refusal.detail);
    }
    anthropic_definition()->release_refusal(NULL, &refusal);
}

static void a_reset_call_that_breaks_its_header_is_malformed(void) {
    const uint32_t answer_size = (uint32_t)sizeof(forebay_anthropic_reset_answer);
    forebay_anthropic_reset_argument argument = SPISOR_INIT(forebay_anthropic_reset_argument);
    expect_reset_malformed("a NULL reset argument", NULL, answer_size, NULL);
    expect_reset_malformed("a reset answer short of its first shape", &argument, (uint32_t)sizeof(uint32_t), NULL);
    argument.size = (uint32_t)sizeof argument.size;
    expect_reset_malformed("a reset argument short of its first shape", &argument, answer_size, NULL);
    argument = (forebay_anthropic_reset_argument)SPISOR_INIT(forebay_anthropic_reset_argument);
    argument.header_count = 1;
    expect_reset_malformed("reset headers NULL beside a count", &argument, answer_size, "headers");
    static const forebay_pair nameless[] = {{NULL, "1"}};
    static const forebay_pair valueless[] = {{"retry-after", NULL}};
    argument.headers = nameless;
    expect_reset_malformed("a reset header without a name", &argument, answer_size, "headers");
    argument.headers = valueless;
    expect_reset_malformed("a reset header without a value", &argument, answer_size, "headers");
    argument = (forebay_anthropic_reset_argument)SPISOR_INIT(forebay_anthropic_reset_argument);
    argument.now = NAN;
    expect_reset_malformed("a reset now that is not a number", &argument, answer_size, "now");
    argument.now = PAST_TWO_TO_THE_53;
    expect_reset_malformed("a reset now past 2^53", &argument, answer_size, "now");
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
    anthropic_definition()->release_reply(NULL, NULL);
    anthropic_definition()->release_route(NULL, NULL);
}

/** A caller compiled before the reply was appended gets the bytes alone, and a release empties all it holds. */
static void an_answer_short_of_the_reply_gets_the_body_alone(void) {
    static const forebay_catalog_model model = {.id = "a", .name = "A"};
    forebay_anthropic_model_listing_argument listing = SPISOR_INIT(forebay_anthropic_model_listing_argument);
    listing.models = &model;
    listing.model_count = 1;
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    forebay_anthropic_body_answer short_answer = SPISOR_INIT(forebay_anthropic_body_answer);
    short_answer.size = (uint32_t)(offsetof(forebay_anthropic_body_answer, storage) + sizeof(void *));
    short_answer.status = 7;
    spisor_status status = anthropic_definition()->model_listing(NULL, &listing, &short_answer, &refusal);
    if (status != SPISOR_OK || short_answer.bytes == NULL || short_answer.status != 7 ||
        short_answer.content_type != NULL) {
        support_failf("a body answer short of the reply", "answered %d, status %u", (int)status,
                      (unsigned)short_answer.status);
    }
    anthropic_definition()->release_body(NULL, &short_answer);
    forebay_anthropic_body_answer answer = SPISOR_INIT(forebay_anthropic_body_answer);
    status = anthropic_definition()->model_listing(NULL, &listing, &answer, &refusal);
    anthropic_definition()->release_body(NULL, &answer);
    if (status != SPISOR_OK || answer.status != 0 || answer.content_type != NULL || answer.headers != NULL ||
        answer.header_count != 0) {
        support_failf("a released body answer", "still holds status %u and %zu headers", (unsigned)answer.status,
                      answer.header_count);
    }
    anthropic_definition()->release_refusal(NULL, &refusal);
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
    int rate_limit_vectors = run_vectors("rate-limit-");
    if (rate_limit_vectors != RATE_LIMIT_VECTOR_COUNT) {
        support_failf("rate-limit vectors", "%d files, expected %d", rate_limit_vectors, RATE_LIMIT_VECTOR_COUNT);
    }
    int reset_time_vectors = run_vectors("reset-time-");
    if (reset_time_vectors != RESET_TIME_VECTOR_COUNT) {
        support_failf("reset-time vectors", "%d files, expected %d", reset_time_vectors, RESET_TIME_VECTOR_COUNT);
    }
    int decode_vectors = run_vectors("decode-");
    if (decode_vectors != DECODE_VECTOR_COUNT) {
        support_failf("decode vectors", "%d files, expected %d", decode_vectors, DECODE_VECTOR_COUNT);
    }
    int route_vectors = run_vectors("route-");
    if (route_vectors != ROUTE_VECTOR_COUNT) {
        support_failf("route vectors", "%d files, expected %d", route_vectors, ROUTE_VECTOR_COUNT);
    }
    int error_envelope_vectors = run_vectors("envelope-");
    if (error_envelope_vectors != ERROR_ENVELOPE_VECTOR_COUNT) {
        support_failf("error-envelope vectors", "%d files, expected %d", error_envelope_vectors,
                      ERROR_ENVELOPE_VECTOR_COUNT);
    }
    int reply_vectors = run_reply_vectors();
    if (reply_vectors != REPLY_VECTOR_COUNT) {
        support_failf("reply vectors", "%d files, expected %d", reply_vectors, REPLY_VECTOR_COUNT);
    }
    the_rate_limit_vectors_cover_the_required_cases();
    a_rate_limit_call_that_breaks_its_header_is_malformed();
    an_argument_short_of_the_en_gb_name_uses_the_en_us_one();
    a_rate_limit_clock_follows_the_offset();
    a_reset_call_that_breaks_its_header_is_malformed();
    run_decode_cases();
    a_root_that_is_no_object_is_answered_as_an_invalid_request();
    an_answer_that_cannot_hold_the_reply_is_refused_a_root_that_is_no_object();
    an_answer_of_the_first_shape_gets_the_request_and_no_forwarded_headers();
    a_decode_that_breaks_its_header_is_malformed();
    a_route_call_that_breaks_its_header_is_malformed();
    a_number_that_is_not_finite_is_written_as_null();
    bytes_that_are_no_json_are_malformed_to_a_response_and_invalid_to_a_request();
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
    a_lookup_answers_found_or_not_found_with_the_proxys_bodies();
    a_lookup_that_breaks_its_header_is_malformed();
    an_answer_short_of_the_reply_gets_the_body_alone();
    releasing_an_answer_never_filled_does_nothing();
    printf("anthropic_test: %d request vectors, %d response vectors\n", vectors, response_vectors);
    return support_summary("anthropic_test");
}
