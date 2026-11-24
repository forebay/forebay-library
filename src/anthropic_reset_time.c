#include "anthropic_reset_time.h"

#include <stdint.h>
#include <string.h>

#include "anthropic_header_names.h"
#include "definition_support.h"
#include "spisor/definition.h"

#define ARGUMENT_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_reset_argument, now)
#define ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_reset_answer, reset_at)

#define MILLISECONDS_PER_SECOND 1000u
#define MILLISECOND_DIGITS 3

static const char *const UNIFIED_RESET_HEADERS[] = {"anthropic-ratelimit-unified-5h-reset",
                                                    "anthropic-ratelimit-unified-reset"};
static const char RETRY_AFTER_HEADER[] = "retry-after";
static const uint64_t EXACT_MILLISECONDS = (uint64_t)FOREBAY_EXACT_LIMIT;

/** A header value with the ASCII whitespace around it trimmed: the characters from `cursor` to `end`. */
typedef struct trimmed_text {
    const char *cursor;
    const char *end;
} trimmed_text;

/** A count of seconds as its whole part and the milliseconds of its fraction, read exactly in decimal. */
typedef struct decimal_seconds {
    uint64_t whole;
    uint64_t fraction_milliseconds;
    int is_above_zero;
} decimal_seconds;

static int is_ascii_space(char character) {
    return character == ' ' || (character >= '\t' && character <= '\r');
}

static int is_digit(char character) {
    return character >= '0' && character <= '9';
}

static trimmed_text trimmed(const char *value) {
    while (is_ascii_space(*value)) {
        value++;
    }
    const char *end = value + strlen(value);
    while (end > value && is_ascii_space(end[-1])) {
        end--;
    }
    return (trimmed_text){value, end};
}

/** Reads `[0-9]+` into the whole seconds; 0 when there is no digit or the seconds are past 2^53 milliseconds. */
static int read_whole(trimmed_text *text, decimal_seconds *seconds) {
    if (text->cursor == text->end || !is_digit(*text->cursor)) {
        return 0;
    }
    for (; text->cursor < text->end && is_digit(*text->cursor); text->cursor++) {
        int digit = *text->cursor - '0';
        seconds->whole = seconds->whole * 10 + (uint64_t)digit;
        seconds->is_above_zero |= digit != 0;
        if (seconds->whole > EXACT_MILLISECONDS / MILLISECONDS_PER_SECOND) {
            return 0;
        }
    }
    return 1;
}

/** Reads `[0-9]+` after the dot, keeping its first three digits as milliseconds; 0 when there is no digit. */
static int read_fraction(trimmed_text *text, decimal_seconds *seconds) {
    int kept = 0;
    if (text->cursor == text->end) {
        return 0;
    }
    for (; text->cursor < text->end; text->cursor++) {
        if (!is_digit(*text->cursor)) {
            return 0;
        }
        int digit = *text->cursor - '0';
        seconds->is_above_zero |= digit != 0;
        if (kept < MILLISECOND_DIGITS) {
            seconds->fraction_milliseconds = seconds->fraction_milliseconds * 10 + (uint64_t)digit;
            kept++;
        }
    }
    for (; kept < MILLISECOND_DIGITS; kept++) {
        seconds->fraction_milliseconds *= 10;
    }
    return 1;
}

/**
 * A header value as milliseconds: after trimming, `+?[0-9]+(.[0-9]+)?` above zero, times 1000 and truncated, in
 * `*milliseconds`; 0 for anything else and for milliseconds past 2^53.
 *
 * @implNote The fraction never passes through a double: its first three digits are the milliseconds and the rest are
 * dropped, which is the truncation, so no length of fraction can round the whole seconds, and `1.005` reads 1005 where
 * a double times 1000 gives 1004.9999.
 */
static int read_milliseconds(const char *value, uint64_t *milliseconds) {
    trimmed_text text = trimmed(value);
    decimal_seconds seconds = {0, 0, 0};
    if (text.cursor < text.end && *text.cursor == '+') {
        text.cursor++;
    }
    if (!read_whole(&text, &seconds)) {
        return 0;
    }
    if (text.cursor < text.end) {
        if (*text.cursor != '.') {
            return 0;
        }
        text.cursor++;
        if (!read_fraction(&text, &seconds)) {
            return 0;
        }
    }
    uint64_t total = seconds.whole * MILLISECONDS_PER_SECOND + seconds.fraction_milliseconds;
    if (!seconds.is_above_zero || total > EXACT_MILLISECONDS) {
        return 0;
    }
    *milliseconds = total;
    return 1;
}

/** The value of the last pair `name` names, ignoring case, NULL when none does. */
static const char *header_value(const forebay_pair *headers, size_t header_count, const char *name) {
    const char *value = NULL;
    for (size_t index = 0; index < header_count; index++) {
        if (anthropic_names_match(headers[index].name, name)) {
            value = headers[index].value;
        }
    }
    return value;
}

/** The latest unified reset header's milliseconds in `*latest`; 0 when neither holds a value. */
static int latest_unified_reset(const forebay_pair *headers, size_t header_count, uint64_t *latest) {
    int found = 0;
    for (size_t index = 0; index < sizeof UNIFIED_RESET_HEADERS / sizeof UNIFIED_RESET_HEADERS[0]; index++) {
        const char *value = header_value(headers, header_count, UNIFIED_RESET_HEADERS[index]);
        uint64_t milliseconds = 0;
        if (value != NULL && read_milliseconds(value, &milliseconds) && (!found || milliseconds > *latest)) {
            *latest = milliseconds;
            found = 1;
        }
    }
    return found;
}

int anthropic_read_reset(const forebay_pair *headers, size_t header_count, double now, double *reset_at) {
    uint64_t milliseconds = 0;
    if (latest_unified_reset(headers, header_count, &milliseconds)) {
        *reset_at = (double)milliseconds;
        return 1;
    }
    const char *retry_after = header_value(headers, header_count, RETRY_AFTER_HEADER);
    if (retry_after == NULL || !read_milliseconds(retry_after, &milliseconds)) {
        return 0;
    }
    double reset = now + (double)milliseconds;
    if (reset > FOREBAY_EXACT_LIMIT) {
        return 0;
    }
    *reset_at = reset;
    return 1;
}

spisor_status anthropic_reset_time(void *context, const forebay_anthropic_reset_argument *argument,
                                   forebay_anthropic_reset_answer *answer, spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, ARGUMENT_SHAPE) || answer == NULL ||
        !spisor_has_shape(answer->size, ANSWER_SHAPE)) {
        return spisor_refuse_shape(refusal, "anthropic's reset_time");
    }
    spisor_status checked =
        forebay_check_pairs(argument->headers, argument->header_count, "headers", "header_count", refusal);
    if (checked == SPISOR_OK) {
        checked = forebay_check_exact_milliseconds(argument->now, "now", refusal);
    }
    if (checked != SPISOR_OK) {
        return checked;
    }
    double reset_at = 0;
    answer->has_reset_at = (uint32_t)anthropic_read_reset(argument->headers, argument->header_count, argument->now,
                                                          &reset_at);
    answer->reset_at = reset_at;
    return SPISOR_OK;
}
