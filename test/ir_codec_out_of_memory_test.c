#include "support.h"

#include "allocation.h"
#include "allocation_failure.h"
#include "ir_json.h"
#include "ir_round_trip.h"
#include "spisor/json_allocation.h"
#include "vector_reader.h"

/**
 * @file
 * Fails every allocation of the test-only IR codec, one at a time and from that one on, over every valid IR document
 * of `test/vectors/ir/` and the IR side of every `test/vectors/anthropic/` vector: the schema form, the read and the
 * write. Each failure must answer OUT_OF_MEMORY from the stage that met it, or, where the failed allocation was not
 * one the round trip needed, the round trip it gives without one, and must leave no block live.
 */

#define ATTEMPT_LIMIT 100000

typedef struct ir_document {
    round_trip_function round_trip;
    cJSON *value;
    const char *path;
} ir_document;

static int is_out_of_memory(round_trip_statuses statuses) {
    return statuses.read == SPISOR_OUT_OF_MEMORY || statuses.write == SPISOR_OUT_OF_MEMORY;
}

static int is_whole_round_trip(round_trip_statuses statuses) {
    return statuses.read == SPISOR_OK && statuses.write == SPISOR_OK;
}

/** One failing round trip: 1 to go on with the next allocation, 0 once it ran whole or failed a check. */
static int fail_round_trip_once(const ir_document *document, const char *label, allocation_failure_mode mode,
                                uint64_t attempt) {
    ir_json_arena arena = {0};
    cJSON *written = NULL;
    int64_t baseline = spisor_memory_live_blocks();
    fail_allocation(mode, attempt);
    cJSON *form = ir_json_schema_form(document->value);
    round_trip_statuses statuses = form == NULL ? statuses_of(SPISOR_OUT_OF_MEMORY, SPISOR_NONE)
                                                : document->round_trip(form, &arena, &written);
    int failed = spisor_memory_failed();
    spisor_memory_fail_at(0);
    int answered = is_out_of_memory(statuses) ? written == NULL : is_whole_round_trip(statuses);
    cJSON_Delete(written);
    ir_json_arena_release(&arena);
    cJSON_Delete(form);
    int64_t leaked = spisor_memory_live_blocks() - baseline;
    if (leaked != 0 || !answered) {
        support_failf(label, "failing allocation %llu read with %d and wrote with %d, leaving %lld blocks",
                      (unsigned long long)attempt, (int)statuses.read, (int)statuses.write, (long long)leaked);
        return 0;
    }
    return failed;
}

static void every_failure_of(const ir_document *document, allocation_failure_mode mode) {
    char label[1100];
    snprintf(label, sizeof label, "%s%s", document->path, allocation_failure_mode_name(mode));
    uint64_t attempt = 1;
    while (attempt < ATTEMPT_LIMIT && fail_round_trip_once(document, label, mode, attempt)) {
        attempt++;
    }
    if (attempt == 1) {
        support_fail(label, "no allocation was ever failed, so nothing was checked");
    }
}

static void every_failure_of_schema_document(const char *path, allocation_failure_mode mode) {
    cJSON *file = spisor_vector_read(path);
    const cJSON *valid = cJSON_GetObjectItemCaseSensitive(file, "valid");
    ir_document document = {
        schema_round_trip(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(file, "definition"))),
        cJSON_GetObjectItemCaseSensitive(file, "value"), path};
    if (document.round_trip == NULL || document.value == NULL || !cJSON_IsBool(valid)) {
        support_fail(path, "not a schema document of a definition the codec reads");
    } else if (cJSON_IsTrue(valid)) {
        every_failure_of(&document, mode);
    }
    cJSON_Delete(file);
}

static void every_failure_of_anthropic_vector(const char *path, allocation_failure_mode mode) {
    if (is_wire_only_vector(path)) {
        return;
    }
    cJSON *file = spisor_vector_read(path);
    const direction_side *side =
        anthropic_side(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(file, "direction")));
    ir_document document = {side == NULL ? NULL : side->round_trip,
                            side == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(file, side->member), path};
    if (document.value == NULL) {
        support_fail(path, "not an anthropic vector with an IR side");
    } else {
        every_failure_of(&document, mode);
    }
    cJSON_Delete(file);
}

static void every_failure_of_each(const char *list_path, void (*run)(const char *path, allocation_failure_mode mode),
                                  allocation_failure_mode mode) {
    char *list = support_read_text(list_path);
    if (list == NULL) {
        support_fail("vector list", list_path);
        return;
    }
    for (char *line = strtok(list, "\r\n"); line != NULL; line = strtok(NULL, "\r\n")) {
        run(line, mode);
    }
    free(list);
}

static void every_failure_of_everything(allocation_failure_mode mode) {
    every_failure_of_each(IR_VECTOR_LIST, every_failure_of_schema_document, mode);
    every_failure_of_each(ANTHROPIC_VECTOR_LIST, every_failure_of_anthropic_vector, mode);
}

int main(void) {
    spisor_json_use_library_allocator();
    in_every_failure_mode(every_failure_of_everything);
    return support_summary("ir_codec_out_of_memory_test");
}
