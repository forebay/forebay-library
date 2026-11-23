#include "anthropic_rate_limit_reply.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "anthropic_header_names.h"
#include "anthropic_operations.h"
#include "definition_support.h"
#include "spisor/allocation.h"
#include "spisor/definition.h"

#define ARGUMENT_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_rate_limit_argument, zone_en_us)
#define EN_GB_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_rate_limit_argument, zone_en_gb)
#define ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_reply_answer, storage)

#define STATUS_HEADER "anthropic-ratelimit-unified-status"
#define RESET_HEADER "anthropic-ratelimit-unified-reset"
#define ADDED_HEADERS 4
#define MESSAGE_FIXED_LIMIT 128
#define NUMBER_TEXT_LIMIT 32
#define MINUTES_PER_DAY 1440
#define MILLISECONDS_PER_MINUTE 60000.0
#define FALLBACK_RETRY_AFTER_SECONDS 60.0
#define REPLY_STATUS 429

/** The largest magnitude a double holds exactly as an integer, which is also what JSON.stringify prints in full. */
static const double EXACT_LIMIT = 9007199254740992.0;

static const char *const DROPPED_HEADERS[] = {"content-encoding", "content-length", "x-hub-rate-limited",
                                              "x-hub-retry-after-ms"};
static const char *const RESET_HEADERS[] = {"anthropic-ratelimit-unified-5h-reset", RESET_HEADER};
static const char LIMIT_MESSAGE[] = "You've hit your usage limit \xc2\xb7";

/** The headers of the reply as they are built: names and values borrowed until they are copied into the answer. */
typedef struct header_set {
    forebay_pair *pairs;
    size_t count;
} header_set;

static int is_one_of(const char *name, const char *const *names, size_t count) {
    for (size_t index = 0; index < count; index++) {
        if (anthropic_names_match(name, names[index])) {
            return 1;
        }
    }
    return 0;
}

static forebay_pair *find_header(const header_set *set, const char *name) {
    for (size_t index = 0; index < set->count; index++) {
        if (anthropic_names_match(set->pairs[index].name, name)) {
            return &set->pairs[index];
        }
    }
    return NULL;
}

/** Sets a header, keeping the position of one already there; the set has room for what the operation adds. */
static void set_header(header_set *set, const char *name, const char *value) {
    forebay_pair *existing = find_header(set, name);
    if (existing != NULL) {
        existing->value = value;
    } else {
        set->pairs[set->count++] = (forebay_pair){name, value};
    }
}

static void set_header_if_absent(header_set *set, const char *name, const char *value) {
    if (find_header(set, name) == NULL) {
        set_header(set, name, value);
    }
}

static void copy_upstream_headers(const forebay_anthropic_rate_limit_argument *argument, header_set *set) {
    for (size_t index = 0; index < argument->upstream_header_count; index++) {
        const forebay_pair *pair = &argument->upstream_headers[index];
        if (!is_one_of(pair->name, DROPPED_HEADERS, sizeof DROPPED_HEADERS / sizeof DROPPED_HEADERS[0])) {
            set_header(set, pair->name, pair->value);
        }
    }
}

static int is_ascii_space(char character) {
    return character == ' ' || (character >= '\t' && character <= '\r');
}

/** The header read as whole seconds in `[+-]digits` after trimming, in milliseconds; 0 for anything else. */
static double header_reset_milliseconds(const char *value) {
    if (value == NULL) {
        return 0;
    }
    while (is_ascii_space(*value)) {
        value++;
    }
    const char *end = value + strlen(value);
    while (end > value && is_ascii_space(end[-1])) {
        end--;
    }
    int negative = value < end && *value == '-';
    if (value < end && (*value == '-' || *value == '+')) {
        value++;
    }
    double seconds = 0;
    for (const char *digit = value; digit < end; digit++) {
        if (*digit < '0' || *digit > '9') {
            return 0;
        }
        seconds = seconds * 10 + (*digit - '0');
        if (seconds > EXACT_LIMIT) {
            return 0;
        }
    }
    double milliseconds = seconds * 1000;
    if (value == end || milliseconds > EXACT_LIMIT) {
        return 0;
    }
    return negative ? -milliseconds : milliseconds;
}

static double reconciled_reset(const header_set *set, const forebay_anthropic_rate_limit_argument *argument) {
    double latest = argument->has_reset_at && argument->reset_at > 0 ? argument->reset_at : 0;
    for (size_t index = 0; index < sizeof RESET_HEADERS / sizeof RESET_HEADERS[0]; index++) {
        const forebay_pair *header = find_header(set, RESET_HEADERS[index]);
        double milliseconds = header == NULL ? 0 : header_reset_milliseconds(header->value);
        latest = milliseconds > latest ? milliseconds : latest;
    }
    return latest;
}

/** A whole number printed as JavaScript's String does, with no sign on a zero. */
static void print_whole_number(double number, char *text) {
    snprintf(text, NUMBER_TEXT_LIMIT, "%.0f", number + 0.0);
}

/** The clock `h:mm AM` the instant reads in a zone `offset_minutes` from UTC. */
static void print_clock(double instant, int64_t offset_minutes, char *text, size_t size) {
    double minutes_of_day = fmod(floor(instant / MILLISECONDS_PER_MINUTE) + (double)offset_minutes, MINUTES_PER_DAY);
    int minute_of_day = (int)(minutes_of_day < 0 ? minutes_of_day + MINUTES_PER_DAY : minutes_of_day);
    int hour = minute_of_day / 60;
    int twelve_hour = hour % 12 == 0 ? 12 : hour % 12;
    snprintf(text, size, "%d:%02d %s", twelve_hour, minute_of_day % 60, hour < 12 ? "AM" : "PM");
}

static int is_offset_name(const char *zone) {
    return strncmp(zone, "GMT+", 4) == 0 || strncmp(zone, "GMT-", 4) == 0;
}

/**
 * The zone name the reply shows.
 *
 * @implNote en-US names only the zones that have an American abbreviation and spells the rest as an offset; en-GB
 * supplies the local one, such as CEST, for those. A zone neither names stays an offset.
 */
static const char *shown_zone(const forebay_anthropic_rate_limit_argument *argument) {
    if (!spisor_has_shape(argument->size, EN_GB_SHAPE) || !is_offset_name(argument->zone_en_us) ||
        is_offset_name(argument->zone_en_gb)) {
        return argument->zone_en_us;
    }
    return argument->zone_en_gb;
}

/** The message of the reply in memory the caller releases, NULL when memory runs out. */
static char *limit_message(double reset, const forebay_anthropic_rate_limit_argument *argument) {
    const char *zone = shown_zone(argument);
    size_t size = strlen(zone) + MESSAGE_FIXED_LIMIT;
    char *message = spisor_memory_allocate(size);
    if (message == NULL) {
        return NULL;
    }
    if (reset > argument->now) {
        char clock[NUMBER_TEXT_LIMIT];
        print_clock(reset, argument->offset_minutes, clock, sizeof clock);
        snprintf(message, size, "%s resets at %s %s", LIMIT_MESSAGE, clock, zone);
    } else {
        snprintf(message, size, "%s try again later", LIMIT_MESSAGE);
    }
    return message;
}

static spisor_status write_body(const char *message, forebay_anthropic_body_answer *body, spisor_refusal *refusal) {
    anthropic_writer writer = {SPISOR_OK, NULL};
    cJSON *wire = anthropic_error_object(&writer, "rate_limit_error", message);
    return anthropic_finish_body(&writer, wire, body, refusal);
}

/** Copies every header into `*storage` as the answer's own. */
static int copy_headers(void **storage, const header_set *set, const forebay_pair **headers) {
    forebay_pair *copies = forebay_storage_array(storage, set->count, sizeof *copies);
    for (size_t index = 0; copies != NULL && index < set->count; index++) {
        if (!forebay_storage_text(storage, set->pairs[index].name, &copies[index].name) ||
            !forebay_storage_text(storage, set->pairs[index].value, &copies[index].value)) {
            return 0;
        }
    }
    *headers = copies;
    return copies != NULL;
}

/** The headers of the reply set over what the upstream sent, or added after it. */
static void set_reply_headers(const forebay_anthropic_rate_limit_argument *argument, double reset, header_set *set,
                              char *retry_after, char *reset_seconds) {
    double retry_seconds =
        reset > argument->now ? floor((reset - argument->now) / 1000 + 0.5) : FALLBACK_RETRY_AFTER_SECONDS;
    print_whole_number(retry_seconds, retry_after);
    print_whole_number(floor((reset > 0 ? reset : argument->now) / 1000), reset_seconds);
    set_header(set, "content-type", "application/json");
    set_header(set, "retry-after", retry_after);
    set_header_if_absent(set, STATUS_HEADER, "rejected");
    set_header_if_absent(set, RESET_HEADER, reset_seconds);
}

/** Fills `answer` from a body and headers that share one storage chain, releasing it when any step fails. */
static spisor_status fill_reply(const forebay_anthropic_rate_limit_argument *argument, double reset, header_set *set,
                                forebay_anthropic_reply_answer *answer, spisor_refusal *refusal) {
    char *message = limit_message(reset, argument);
    if (message == NULL) {
        return SPISOR_OUT_OF_MEMORY;
    }
    forebay_anthropic_body_answer body = SPISOR_INIT(forebay_anthropic_body_answer);
    spisor_status status = write_body(message, &body, refusal);
    spisor_memory_release(message);
    if (status != SPISOR_OK) {
        return status;
    }
    char retry_after[NUMBER_TEXT_LIMIT];
    char reset_seconds[NUMBER_TEXT_LIMIT];
    set_reply_headers(argument, reset, set, retry_after, reset_seconds);
    const forebay_pair *headers = NULL;
    if (!copy_headers(&body.storage, set, &headers)) {
        forebay_storage_release(body.storage);
        return SPISOR_OUT_OF_MEMORY;
    }
    answer->status = REPLY_STATUS;
    answer->headers = headers;
    answer->header_count = set->count;
    answer->body = (const char *)body.bytes;
    answer->storage = body.storage;
    return SPISOR_OK;
}

static spisor_status answer_reply(const forebay_anthropic_rate_limit_argument *argument,
                                  forebay_anthropic_reply_answer *answer, spisor_refusal *refusal) {
    size_t capacity = argument->upstream_header_count + ADDED_HEADERS;
    header_set set = {spisor_memory_allocate_zeroed(capacity, sizeof(forebay_pair)), 0};
    if (set.pairs == NULL) {
        return SPISOR_OUT_OF_MEMORY;
    }
    copy_upstream_headers(argument, &set);
    double reset = reconciled_reset(&set, argument);
    spisor_status status = fill_reply(argument, reset, &set, answer, refusal);
    spisor_memory_release(set.pairs);
    return status;
}

static int is_exact_number(double number) {
    return forebay_is_finite(number) && fabs(number) <= EXACT_LIMIT;
}

static int are_pairs(const forebay_pair *pairs, size_t count) {
    for (size_t index = 0; index < count; index++) {
        if (pairs[index].name == NULL || pairs[index].value == NULL) {
            return 0;
        }
    }
    return 1;
}

static spisor_status check_argument(const forebay_anthropic_rate_limit_argument *argument, spisor_refusal *refusal) {
    if (argument->upstream_headers == NULL && argument->upstream_header_count > 0) {
        return forebay_refuse_member(refusal, "upstream_headers", "a pointer to upstream_header_count pairs");
    }
    if (!are_pairs(argument->upstream_headers, argument->upstream_header_count)) {
        return forebay_refuse_member(refusal, "upstream_headers", "pairs that each carry a name and a value");
    }
    if (argument->has_reset_at && !is_exact_number(argument->reset_at)) {
        return forebay_refuse_member(refusal, "reset_at", "a finite number of at most 2^53 milliseconds");
    }
    if (!is_exact_number(argument->now)) {
        return forebay_refuse_member(refusal, "now", "a finite number of at most 2^53 milliseconds");
    }
    if (argument->offset_minutes < -MINUTES_PER_DAY || argument->offset_minutes > MINUTES_PER_DAY) {
        return forebay_refuse_member(refusal, "offset_minutes", "a zone offset within 1440 minutes of UTC");
    }
    if (!forebay_is_text(argument->zone_en_us)) {
        return forebay_refuse_member(refusal, "zone_en_us", "a zone's en-US short name, such as UTC");
    }
    if (spisor_has_shape(argument->size, EN_GB_SHAPE) && !forebay_is_text(argument->zone_en_gb)) {
        return forebay_refuse_member(refusal, "zone_en_gb", "a zone's en-GB short name, such as UTC");
    }
    return SPISOR_OK;
}

spisor_status anthropic_rate_limit_reply(void *context, const forebay_anthropic_rate_limit_argument *argument,
                                         forebay_anthropic_reply_answer *answer, spisor_refusal *refusal) {
    (void)context;
    if (argument == NULL || !spisor_has_shape(argument->size, ARGUMENT_SHAPE) ||
        !SPISOR_IS_EMPTY_COUNTED_ANSWER(answer, ANSWER_SHAPE, answer->header_count)) {
        return spisor_refuse_shape(refusal, "anthropic's rate_limit_reply");
    }
    spisor_status checked = check_argument(argument, refusal);
    return checked == SPISOR_OK ? answer_reply(argument, answer, refusal) : checked;
}

ANTHROPIC_RELEASE_ANSWER(anthropic_release_reply, forebay_anthropic_reply_answer, ANSWER_SHAPE)
