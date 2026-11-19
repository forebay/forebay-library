#include "support.h"

#include "ir_json.h"
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

/** How a round trip went: the read's status, and the write's, which is only tried after a read that is OK. */
typedef struct round_trip_statuses {
    spisor_status read;
    spisor_status write;
} round_trip_statuses;

typedef round_trip_statuses (*round_trip_function)(const cJSON *json, ir_json_arena *arena, cJSON **written);

static round_trip_statuses statuses_of(spisor_status read, spisor_status write) {
    round_trip_statuses statuses = {read, write};
    return statuses;
}

static round_trip_statuses request_round_trip(const cJSON *json, ir_json_arena *arena, cJSON **written) {
    forebay_ir_request request;
    spisor_status status = ir_json_read_request(json, arena, &request);
    return statuses_of(status, status == SPISOR_OK ? ir_json_write_request(&request, written) : SPISOR_NONE);
}

static round_trip_statuses response_round_trip(const cJSON *json, ir_json_arena *arena, cJSON **written) {
    forebay_ir_response response;
    spisor_status status = ir_json_read_response(json, arena, &response);
    return statuses_of(status, status == SPISOR_OK ? ir_json_write_response(&response, written) : SPISOR_NONE);
}

static round_trip_statuses block_round_trip(const cJSON *json, ir_json_arena *arena, cJSON **written) {
    forebay_ir_block block;
    spisor_status status = ir_json_read_block(json, arena, &block);
    return statuses_of(status, status == SPISOR_OK ? ir_json_write_block(&block, written) : SPISOR_NONE);
}

static round_trip_statuses event_round_trip(const cJSON *json, ir_json_arena *arena, cJSON **written) {
    forebay_ir_event event;
    spisor_status status = ir_json_read_event(json, arena, &event);
    return statuses_of(status, status == SPISOR_OK ? ir_json_write_event(&event, written) : SPISOR_NONE);
}

static round_trip_statuses events_round_trip(const cJSON *json, ir_json_arena *arena, cJSON **written) {
    const forebay_ir_event *events = NULL;
    size_t count = 0;
    spisor_status status = ir_json_read_events(json, arena, &events, &count);
    return statuses_of(status, status == SPISOR_OK ? ir_json_write_events(events, count, written) : SPISOR_NONE);
}

static round_trip_statuses failure_round_trip(const cJSON *json, ir_json_arena *arena, cJSON **written) {
    forebay_ir_failure failure;
    spisor_status status = ir_json_read_failure(json, arena, &failure);
    return statuses_of(status, status == SPISOR_OK ? ir_json_write_failure(&failure, written) : SPISOR_NONE);
}

typedef struct named_round_trip {
    const char *name;
    round_trip_function round_trip;
} named_round_trip;

static const named_round_trip SCHEMA_DEFINITIONS[] = {
    {"IrRequest", request_round_trip}, {"IrResponse", response_round_trip}, {"IrBlock", block_round_trip},
    {"IrStreamEvent", event_round_trip}, {"IrFailure", failure_round_trip},
};

/** The anthropic directions, with the vector member that holds the IR side. */
typedef struct direction_side {
    const char *direction;
    const char *member;
    round_trip_function round_trip;
} direction_side;

static const direction_side ANTHROPIC_DIRECTIONS[] = {
    {"decodeRequest", "output", request_round_trip},   {"encodeRequest", "input", request_round_trip},
    {"decodeResponse", "output", response_round_trip}, {"encodeResponse", "input", response_round_trip},
    {"decodeStream", "output", events_round_trip},     {"encodeStream", "input", events_round_trip},
};

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

static round_trip_function schema_round_trip(const char *definition) {
    for (size_t index = 0; definition != NULL && index < sizeof SCHEMA_DEFINITIONS / sizeof SCHEMA_DEFINITIONS[0];
         index++) {
        if (strcmp(SCHEMA_DEFINITIONS[index].name, definition) == 0) {
            return SCHEMA_DEFINITIONS[index].round_trip;
        }
    }
    return NULL;
}

static const direction_side *anthropic_side(const char *direction) {
    for (size_t index = 0; direction != NULL && index < sizeof ANTHROPIC_DIRECTIONS / sizeof ANTHROPIC_DIRECTIONS[0];
         index++) {
        if (strcmp(ANTHROPIC_DIRECTIONS[index].direction, direction) == 0) {
            return &ANTHROPIC_DIRECTIONS[index];
        }
    }
    return NULL;
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
            count++;
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
    printf("ir_codec_test: %d schema documents, %d anthropic vectors\n", documents, vectors);
    return support_summary("ir_codec_test");
}
