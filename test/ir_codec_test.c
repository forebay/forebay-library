#include "support.h"

#include "ir_json.h"
#include "ir_round_trip.h"
#include "spisor/json_allocation.h"
#include "spisor/json_kind.h"
#include "vector_reader.h"

/**
 * @file
 * The test-only IR codec proven on every IR document the repo holds before any translation uses it. Each schema
 * document in `test/vectors/ir/` and the IR side of every `test/vectors/anthropic/` vector (a decode vector's
 * `output`, an encode vector's `input`, a list of events for a stream) is read into forebay/ir.h's structs and written
 * back, and what comes back must equal what went in once both sides are in the schema form. A schema document marked
 * `"valid": false` must instead be refused as MALFORMED by the reader. Since the schema form is applied to both sides
 * alike, a round trip cannot see a defect in the form itself, so the form is also checked on its own cases.
 *
 * The schema form, applied to both sides of every comparison and nothing more: a null object member is dropped,
 * except under the opaque keys `extensions`, `raw`, `input`, `inputSchema` and `metadata`, whose value is kept
 * exactly, nulls inside it included; a thinking block's null `text` becomes "", an image block's null `mediaType` "",
 * and a tool result block's null `content` []. The anthropic vectors were recorded through an old Java serializer that
 * printed absent members as null and broke those three required members with null; this is the normalisation
 * `anthropic-translator`'s own test applies (`toSchemaForm` and `requiredByTheSchema`).
 */

#define IR_DOCUMENT_COUNT 24
#define ANTHROPIC_VECTOR_COUNT 41

static void expect_same(const cJSON *expected, const cJSON *written, const char *label) {
    cJSON *written_form = ir_json_schema_form(written);
    if (!cJSON_Compare(expected, written_form, 1)) {
        char *wanted = cJSON_PrintUnformatted(expected);
        char *got = cJSON_PrintUnformatted(written_form);
        support_failf(label, "the round trip changed the IR\n  expected %s\n  actual   %s", wanted, got);
        cJSON_free(wanted);
        cJSON_free(got);
    }
    cJSON_Delete(written_form);
}

static void check_round_trip(round_trip_function round_trip, const cJSON *value, int valid, const char *label) {
    cJSON *form = ir_json_schema_form(value);
    ir_json_arena arena = {0};
    cJSON *written = NULL;
    round_trip_statuses statuses = form == NULL ? statuses_of(SPISOR_OUT_OF_MEMORY, SPISOR_NONE)
                                                : round_trip(form, &arena, &written);
    if (!valid && statuses.read != SPISOR_MALFORMED) {
        support_failf(label, "an invalid document read with status %d, not MALFORMED", (int)statuses.read);
    } else if (valid && (statuses.read != SPISOR_OK || statuses.write != SPISOR_OK)) {
        support_failf(label, "the round trip read with status %d and wrote with %d", (int)statuses.read,
                      (int)statuses.write);
    } else if (valid) {
        expect_same(form, written, label);
    }
    cJSON_Delete(written);
    ir_json_arena_release(&arena);
    cJSON_Delete(form);
}

static void run_schema_document(const char *path) {
    cJSON *document = spisor_vector_read(path);
    const cJSON *valid = cJSON_GetObjectItemCaseSensitive(document, "valid");
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(document, "value");
    round_trip_function round_trip =
        schema_round_trip(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(document, "definition")));
    if (round_trip == NULL || !cJSON_IsBool(valid) || value == NULL) {
        support_fail(path, "not a schema document of a definition the codec reads");
    } else {
        check_round_trip(round_trip, value, cJSON_IsTrue(valid), path);
    }
    cJSON_Delete(document);
}

static void run_anthropic_vector(const char *path) {
    if (is_wire_only_vector(path)) {
        return;
    }
    cJSON *vector = spisor_vector_read(path);
    const direction_side *side =
        anthropic_side(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(vector, "direction")));
    const cJSON *value = side == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(vector, side->member);
    if (value == NULL) {
        support_fail(path, "not an anthropic vector with an IR side");
    } else {
        check_round_trip(side->round_trip, value, 1, path);
    }
    cJSON_Delete(vector);
}

static int run_every_vector(const char *list_path, void (*run)(const char *path)) {
    char *list = support_read_text(list_path);
    if (list == NULL) {
        support_fail("vector list", list_path);
        return 0;
    }
    int count = 0;
    for (char *line = strtok(list, "\r\n"); line != NULL; line = strtok(NULL, "\r\n")) {
        if (line[0] != '\0') {
            run(line);
            count += is_wire_only_vector(line) ? 0 : 1;
        }
    }
    free(list);
    return count;
}

/** One schema form case: `expected` NULL when the form must equal what was given. */
typedef struct schema_form_case {
    const char *label;
    const char *given;
    const char *expected;
} schema_form_case;

static const schema_form_case SCHEMA_FORM_CASES[] = {
    {"a null member is dropped and a null item kept", "{\"a\":null,\"b\":[null,{\"c\":null}]}", "{\"b\":[null,{}]}"},
    {"every opaque key keeps its nulls",
     "{\"extensions\":{\"a\":null},\"raw\":null,\"input\":null,\"inputSchema\":{\"b\":[null]},"
     "\"metadata\":null}",
     NULL},
    {"an opaque value is not filled inside", "{\"extensions\":{\"kind\":\"thinking\",\"text\":null}}", NULL},
    {"a thinking block's null text becomes empty", "{\"kind\":\"thinking\",\"text\":null,\"signature\":null}",
     "{\"kind\":\"thinking\",\"text\":\"\"}"},
    {"an image block's null mediaType becomes empty", "{\"kind\":\"image\",\"mediaType\":null,\"url\":null}",
     "{\"kind\":\"image\",\"mediaType\":\"\"}"},
    {"a tool result block's null content becomes empty",
     "{\"kind\":\"tool_result\",\"toolUseId\":\"t\",\"content\":null}",
     "{\"kind\":\"tool_result\",\"toolUseId\":\"t\",\"content\":[]}"},
    {"a text block's null text is dropped, not filled", "{\"kind\":\"text\",\"text\":null}", "{\"kind\":\"text\"}"},
};

static cJSON *parse(const char *text) {
    int out_of_memory = 0;
    return spisor_parse_json_reporting((const uint8_t *)text, strlen(text), &out_of_memory);
}

static void check_schema_form(const schema_form_case *form_case) {
    cJSON *given = parse(form_case->given);
    cJSON *expected = parse(form_case->expected == NULL ? form_case->given : form_case->expected);
    cJSON *form = ir_json_schema_form(given);
    if (given == NULL || expected == NULL || !cJSON_Compare(form, expected, 1)) {
        char *got = form == NULL ? NULL : cJSON_PrintUnformatted(form);
        support_failf(form_case->label, "the schema form is %s", got == NULL ? "missing" : got);
        cJSON_free(got);
    }
    cJSON_Delete(form);
    cJSON_Delete(expected);
    cJSON_Delete(given);
}

/** A null opaque member is a node of kind NULL in the structs, so it survives as null rather than vanishing. */
static void keeps_a_null_opaque_member(void) {
    cJSON *block = parse("{\"kind\":\"tool_use\",\"id\":\"t\",\"name\":\"n\",\"input\":null,\"extensions\":null}");
    check_round_trip(block_round_trip, block, 1, "a tool use whose input and extensions are null");
    cJSON_Delete(block);
}

/** Reads `text` as a request, failure or other document through `round_trip` and answers its read status. */
static spisor_status read_status_of(round_trip_function round_trip, const char *text) {
    cJSON *json = parse(text);
    ir_json_arena arena = {0};
    cJSON *written = NULL;
    spisor_status status = json == NULL ? SPISOR_OUT_OF_MEMORY : round_trip(json, &arena, &written).read;
    cJSON_Delete(written);
    ir_json_arena_release(&arena);
    cJSON_Delete(json);
    return status;
}

static void expect_read_status(round_trip_function round_trip, const char *text, spisor_status expected,
                               const char *label) {
    spisor_status status = read_status_of(round_trip, text);
    if (status != expected) {
        support_failf(label, "read with status %d, expected %d", (int)status, (int)expected);
    }
}

/** A null member that is not opaque reads as an absent one, so a null `toolChoice` leaves no choice behind. */
static void reads_a_null_member_as_absent(void) {
    cJSON *json = parse("{\"model\":\"m\",\"stream\":false,\"messages\":[],\"toolChoice\":null}");
    ir_json_arena arena = {0};
    forebay_ir_request request = {0};
    spisor_status status = json == NULL ? SPISOR_OUT_OF_MEMORY : ir_json_read_request(json, &arena, &request);
    if (status != SPISOR_OK || request.tool_choice != NULL) {
        support_failf("a null toolChoice", "read with status %d and a choice %s", (int)status,
                      request.tool_choice == NULL ? "absent" : "present");
    }
    ir_json_arena_release(&arena);
    cJSON_Delete(json);
}

/**
 * @implNote Built by hand: the parser keeps only the last of a key given twice, so a repeated header name can only
 * reach the reader in a tree that was not parsed.
 */
static void refuses_a_repeated_header_name(void) {
    cJSON *failure = parse("{\"status\":429,\"body\":\"\"}");
    cJSON *headers = cJSON_CreateObject();
    cJSON_AddItemToObject(headers, "a", cJSON_CreateString("1"));
    cJSON_AddItemToObject(headers, "a", cJSON_CreateString("2"));
    cJSON_AddItemToObject(failure, "headers", headers);
    ir_json_arena arena = {0};
    forebay_ir_failure read;
    spisor_status status = ir_json_read_failure(failure, &arena, &read);
    if (status != SPISOR_MALFORMED) {
        support_failf("a repeated header name", "read with status %d, not MALFORMED", (int)status);
    }
    ir_json_arena_release(&arena);
    cJSON_Delete(failure);
    expect_read_status(failure_round_trip, "{\"status\":429,\"headers\":{\"a\":\"1\",\"b\":\"2\"},\"body\":\"\"}",
                       SPISOR_OK, "two different header names");
}

static void reads_a_failure_as_a_sized_value(void) {
    cJSON *json = parse("{\"status\":429,\"headers\":{},\"body\":\"\",\"retryAfterMilliseconds\":1500}");
    ir_json_arena arena = {0};
    forebay_ir_failure failure = {0};
    spisor_status status = json == NULL ? SPISOR_OUT_OF_MEMORY : ir_json_read_failure(json, &arena, &failure);
    if (status != SPISOR_OK || failure.size != sizeof(forebay_ir_failure) || failure.has_retry_after != 1 ||
        failure.retry_after_milliseconds != 1500.0) {
        support_failf("a failure", "read with status %d, size %u and a wait of %g", (int)status,
                      (unsigned)failure.size, failure.retry_after_milliseconds);
    }
    ir_json_arena_release(&arena);
    cJSON_Delete(json);
}

static void refuses_extensions_that_are_not_a_record(void) {
    const char *const bad[] = {
        "{\"model\":\"m\",\"stream\":false,\"messages\":[],\"extensions\":\"x\"}",
        "{\"model\":\"m\",\"stream\":false,\"messages\":[{\"role\":\"user\",\"content\":[],\"extensions\":[]}]}",
    };
    for (size_t index = 0; index < sizeof bad / sizeof bad[0]; index++) {
        expect_read_status(request_round_trip, bad[index], SPISOR_MALFORMED, "extensions that are not a record");
    }
    expect_read_status(request_round_trip, "{\"model\":\"m\",\"stream\":false,\"messages\":[],\"extensions\":null}",
                       SPISOR_OK, "null extensions");
    expect_read_status(block_round_trip, "{\"kind\":\"text\",\"text\":\"t\",\"extensions\":5}", SPISOR_MALFORMED,
                       "a block's extensions that are a number");
}

static void expect_count(const char *what, int count, int expected) {
    if (count != expected) {
        support_failf(what, "%d files, expected %d", count, expected);
    }
}

int main(void) {
    spisor_json_use_library_allocator();
    int documents = run_every_vector(IR_VECTOR_LIST, run_schema_document);
    int vectors = run_every_vector(ANTHROPIC_VECTOR_LIST, run_anthropic_vector);
    expect_count("ir documents", documents, IR_DOCUMENT_COUNT);
    expect_count("anthropic vectors", vectors, ANTHROPIC_VECTOR_COUNT);
    for (size_t index = 0; index < sizeof SCHEMA_FORM_CASES / sizeof SCHEMA_FORM_CASES[0]; index++) {
        check_schema_form(&SCHEMA_FORM_CASES[index]);
    }
    keeps_a_null_opaque_member();
    reads_a_null_member_as_absent();
    refuses_a_repeated_header_name();
    reads_a_failure_as_a_sized_value();
    refuses_extensions_that_are_not_a_record();
    printf("ir_codec_test: %d schema documents, %d anthropic vectors\n", documents, vectors);
    return support_summary("ir_codec_test");
}
