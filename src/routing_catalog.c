#include "routing_catalog.h"

#include <string.h>

#include "answer_storage.h"
#include "definition_support.h"
#include "routing_limits.h"
#include "spisor/definition.h"

#define CATALOG_SHAPE SPISOR_SHAPE_THROUGH(forebay_catalog_argument, message)
#define CATALOG_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_catalog_answer, storage)
#define UNASKED_SHAPE SPISOR_SHAPE_THROUGH(forebay_catalog_argument, unasked)
#define DROP_SHAPE SPISOR_SHAPE_THROUGH(forebay_catalog_answer, drop)
#define CHANGED_SHAPE SPISOR_SHAPE_THROUGH(forebay_catalog_argument, changed)

static const char NO_MODELS_WARNING[] = "a provider answered no models: ";
static const char NO_MAP_WARNING[] = "the model map could not be derived, so routing keeps the last one: ";

static int is_event(uint32_t event) {
    return event >= FOREBAY_CATALOG_PROVIDERS_CHANGED && event <= FOREBAY_CATALOG_READ;
}

static int needs_message(const forebay_catalog_argument *argument) {
    return argument->event == FOREBAY_CATALOG_DERIVE_FAILED ||
           (argument->event == FOREBAY_CATALOG_ANSWERED && argument->failed);
}

/** Whether the answering implementation is one the current round did not ask; an argument short of it says no. */
static int is_unasked(const forebay_catalog_argument *argument) {
    return argument->event == FOREBAY_CATALOG_ANSWERED && spisor_has_shape(argument->size, UNASKED_SHAPE) &&
           argument->unasked;
}

static int is_catalog_argument(const forebay_catalog_argument *argument) {
    return is_event(argument->event) && forebay_is_finite(argument->now) &&
           (!argument->asked || forebay_is_finite(argument->last_asked)) &&
           (!needs_message(argument) || argument->message != NULL);
}

/** The bytes of an answer of `size` to empty: through its size, at most the whole struct. */
static size_t cleared_shape(uint32_t size) {
    return size < sizeof(forebay_catalog_answer) ? size : sizeof(forebay_catalog_answer);
}

static forebay_limits_answer read_limits(void) {
    forebay_limits_answer limits = SPISOR_INIT(forebay_limits_answer);
    routing_read_limits(&limits);
    return limits;
}

/** Whether a request finding no model may ask again: the last round began at least the rederive interval ago. */
static int is_past_the_interval(const forebay_catalog_argument *argument) {
    forebay_limits_answer limits = read_limits();
    return !argument->asked || argument->now - argument->last_asked >= (double)limits.rederive_interval_milliseconds;
}

/** Whether a read's ordered list of implementations differs from the one last asked; a short argument says no. */
static int is_changed(const forebay_catalog_argument *argument) {
    return spisor_has_shape(argument->size, CHANGED_SHAPE) && argument->changed;
}

/** What to do about an event that may start a round: join the open one, start one, or nothing. */
static uint32_t action_of(const forebay_catalog_argument *argument) {
    if (argument->asking) {
        return FOREBAY_CATALOG_WAIT;
    }
    if (argument->event == FOREBAY_CATALOG_NO_MODEL && !is_past_the_interval(argument)) {
        return FOREBAY_CATALOG_NOTHING;
    }
    if (argument->event == FOREBAY_CATALOG_READ && !is_changed(argument)) {
        return FOREBAY_CATALOG_NOTHING;
    }
    return FOREBAY_CATALOG_ASK;
}

/** The one warning line of `prefix` and the message, in the answer's storage; 0 when memory runs out. */
static int warn(forebay_catalog_answer *answer, const char *prefix, const char *message) {
    size_t prefix_length = strlen(prefix);
    size_t message_length = strlen(message);
    forebay_log_line *line = forebay_storage_array(&answer->storage, 1, sizeof *line);
    char *text = line == NULL ? NULL : forebay_storage_allocate(&answer->storage, prefix_length + message_length + 1);
    if (text == NULL) {
        forebay_storage_release(answer->storage);
        return 0;
    }
    memcpy(text, prefix, prefix_length);
    memcpy(text + prefix_length, message, message_length + 1);
    line->level = FOREBAY_LOG_WARN;
    line->message = text;
    answer->log = line;
    answer->log_count = 1;
    return 1;
}

static spisor_status decide_answered(const forebay_catalog_argument *argument, forebay_catalog_answer *answer) {
    if (is_unasked(argument)) {
        answer->drop = 1;
    } else if (argument->failed) {
        answer->keep_last = 1;
    } else {
        answer->rederive = argument->round_closed ? 1u : 0u;
    }
    if (!argument->failed) {
        return SPISOR_OK;
    }
    return warn(answer, NO_MODELS_WARNING, argument->message) ? SPISOR_OK : SPISOR_OUT_OF_MEMORY;
}

static spisor_status decide(const forebay_catalog_argument *argument, forebay_catalog_answer *answer) {
    answer->action = FOREBAY_CATALOG_NOTHING;
    switch (argument->event) {
    case FOREBAY_CATALOG_ANSWERED:
        return decide_answered(argument, answer);
    case FOREBAY_CATALOG_ROUND_CLOSED:
        answer->rederive = 1;
        return SPISOR_OK;
    case FOREBAY_CATALOG_DERIVE_FAILED:
        answer->keep_last = 1;
        return warn(answer, NO_MAP_WARNING, argument->message) ? SPISOR_OK : SPISOR_OUT_OF_MEMORY;
    default:
        answer->action = action_of(argument);
        if (answer->action == FOREBAY_CATALOG_ASK) {
            answer->budget_milliseconds = read_limits().models_budget_milliseconds;
        }
        return SPISOR_OK;
    }
}

spisor_status routing_catalog_refresh(void *context, const forebay_catalog_argument *argument,
                                      forebay_catalog_answer *answer, spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, CATALOG_SHAPE) ||
        !SPISOR_IS_EMPTY_COUNTED_ANSWER(answer, CATALOG_ANSWER_SHAPE, answer->log_count)) {
        return spisor_refuse_shape(refusal, "routing's catalog");
    }
    if (!is_catalog_argument(argument)) {
        return forebay_refuse_member(refusal, "event, now, last_asked or message",
                                     "a forebay_catalog_event at a finite now, with the message it needs");
    }
    if (is_unasked(argument) && !spisor_has_shape(answer->size, DROP_SHAPE)) {
        return forebay_refuse_member(refusal, "drop", "an answer that reaches drop, for an answer of an unasked one");
    }
    spisor_status status = decide(argument, answer);
    if (status != SPISOR_OK) {
        SPISOR_CLEAR_ANSWER(answer, cleared_shape(answer->size));
    }
    return status;
}

void routing_release_catalog_answer(void *context, forebay_catalog_answer *answer) {
    (void)context;
    if (answer != NULL && spisor_has_shape(answer->size, CATALOG_ANSWER_SHAPE)) {
        forebay_storage_release(answer->storage);
        SPISOR_CLEAR_ANSWER(answer, cleared_shape(answer->size));
    }
}
