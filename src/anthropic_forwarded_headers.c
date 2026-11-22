#include "anthropic_forwarded_headers.h"

#include <ctype.h>
#include <string.h>

#include "answer_storage.h"

static const char *const NOT_FORWARDED[] = {"authorization",
                                            "x-api-key",
                                            "host",
                                            "content-length",
                                            "accept-encoding",
                                            "connection",
                                            "keep-alive",
                                            "proxy-authenticate",
                                            "proxy-authorization",
                                            "proxy-connection",
                                            "te",
                                            "trailer",
                                            "transfer-encoding",
                                            "upgrade"};

static int is_not_forwarded(const char *lowered) {
    for (size_t index = 0; index < sizeof NOT_FORWARDED / sizeof NOT_FORWARDED[0]; index++) {
        if (strcmp(lowered, NOT_FORWARDED[index]) == 0) {
            return 1;
        }
    }
    return 0;
}

static const char *lowered_copy(void **storage, const char *name) {
    size_t length = strlen(name);
    char *copy = forebay_storage_allocate(storage, length + 1);
    for (size_t index = 0; copy != NULL && index < length; index++) {
        copy[index] = (char)tolower((unsigned char)name[index]);
    }
    return copy;
}

static forebay_pair *pair_named(forebay_pair *pairs, size_t count, const char *lowered) {
    for (size_t index = 0; index < count; index++) {
        if (strcmp(pairs[index].name, lowered) == 0) {
            return &pairs[index];
        }
    }
    return NULL;
}

int anthropic_forward_headers(void **storage, const forebay_pair *headers, size_t count,
                              const forebay_pair **forwarded, size_t *forwarded_count) {
    forebay_pair *kept = forebay_storage_array(storage, count, sizeof *kept);
    size_t kept_count = 0;
    if (kept == NULL && count > 0) {
        return 0;
    }
    for (size_t index = 0; index < count; index++) {
        const char *lowered = lowered_copy(storage, headers[index].name);
        const char *value = NULL;
        if (lowered == NULL || !forebay_storage_text(storage, headers[index].value, &value)) {
            return 0;
        }
        if (is_not_forwarded(lowered)) {
            continue;
        }
        forebay_pair *existing = pair_named(kept, kept_count, lowered);
        if (existing != NULL) {
            existing->value = value;
        } else {
            kept[kept_count++] = (forebay_pair){lowered, value};
        }
    }
    *forwarded = kept_count == 0 ? NULL : kept;
    *forwarded_count = kept_count;
    return 1;
}
