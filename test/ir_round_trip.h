#ifndef FOREBAY_TEST_IR_ROUND_TRIP_H
#define FOREBAY_TEST_IR_ROUND_TRIP_H

#include <string.h>

#include "ir_json.h"

/**
 * @file
 * The IR codec's read-then-write round trip per IR definition, and which IR side each anthropic vector direction holds,
 * shared by the codec's round trip test and its out-of-memory test.
 */

/** How a round trip went: the read's status, and the write's, which is only tried after a read that is OK. */
typedef struct round_trip_statuses {
    spisor_status read;
    spisor_status write;
} round_trip_statuses;

typedef round_trip_statuses (*round_trip_function)(const cJSON *json, ir_json_arena *arena, cJSON **written);

static inline round_trip_statuses statuses_of(spisor_status read, spisor_status write) {
    round_trip_statuses statuses = {read, write};
    return statuses;
}

static inline round_trip_statuses request_round_trip(const cJSON *json, ir_json_arena *arena, cJSON **written) {
    forebay_ir_request request;
    spisor_status status = ir_json_read_request(json, arena, &request);
    return statuses_of(status, status == SPISOR_OK ? ir_json_write_request(&request, written) : SPISOR_NONE);
}

static inline round_trip_statuses response_round_trip(const cJSON *json, ir_json_arena *arena, cJSON **written) {
    forebay_ir_response response;
    spisor_status status = ir_json_read_response(json, arena, &response);
    return statuses_of(status, status == SPISOR_OK ? ir_json_write_response(&response, written) : SPISOR_NONE);
}

static inline round_trip_statuses block_round_trip(const cJSON *json, ir_json_arena *arena, cJSON **written) {
    forebay_ir_block block;
    spisor_status status = ir_json_read_block(json, arena, &block);
    return statuses_of(status, status == SPISOR_OK ? ir_json_write_block(&block, written) : SPISOR_NONE);
}

static inline round_trip_statuses event_round_trip(const cJSON *json, ir_json_arena *arena, cJSON **written) {
    forebay_ir_event event;
    spisor_status status = ir_json_read_event(json, arena, &event);
    return statuses_of(status, status == SPISOR_OK ? ir_json_write_event(&event, written) : SPISOR_NONE);
}

static inline round_trip_statuses events_round_trip(const cJSON *json, ir_json_arena *arena, cJSON **written) {
    const forebay_ir_event *events = NULL;
    size_t count = 0;
    spisor_status status = ir_json_read_events(json, arena, &events, &count);
    return statuses_of(status, status == SPISOR_OK ? ir_json_write_events(events, count, written) : SPISOR_NONE);
}

static inline round_trip_statuses failure_round_trip(const cJSON *json, ir_json_arena *arena, cJSON **written) {
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

static inline round_trip_function schema_round_trip(const char *definition) {
    for (size_t index = 0; definition != NULL && index < sizeof SCHEMA_DEFINITIONS / sizeof SCHEMA_DEFINITIONS[0];
         index++) {
        if (strcmp(SCHEMA_DEFINITIONS[index].name, definition) == 0) {
            return SCHEMA_DEFINITIONS[index].round_trip;
        }
    }
    return NULL;
}

/** Whether a vector of the anthropic list is the service's own, not the translator's, which the codec's tests leave. */
static inline int is_wire_only_vector(const char *path) {
    static const char *const prefixes[] = {"rate-limit-", "reset-time-", "decode-", "route-", "envelope-", "reply-"};
    const char *name = path;
    for (const char *cursor = path; *cursor != '\0'; cursor++) {
        name = *cursor == '/' || *cursor == '\\' ? cursor + 1 : name;
    }
    for (size_t index = 0; index < sizeof prefixes / sizeof prefixes[0]; index++) {
        if (strncmp(name, prefixes[index], strlen(prefixes[index])) == 0) {
            return 1;
        }
    }
    return 0;
}

static inline const direction_side *anthropic_side(const char *direction) {
    for (size_t index = 0; direction != NULL && index < sizeof ANTHROPIC_DIRECTIONS / sizeof ANTHROPIC_DIRECTIONS[0];
         index++) {
        if (strcmp(ANTHROPIC_DIRECTIONS[index].direction, direction) == 0) {
            return &ANTHROPIC_DIRECTIONS[index];
        }
    }
    return NULL;
}

#endif
