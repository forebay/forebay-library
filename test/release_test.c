#include <stdint.h>
#include <stdio.h>
#include <string.h>

int32_t forebay_library_routing_tiers(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);
void forebay_library_release(uint8_t *buffer);

static const char TIERS_CALL[] =
    "{\"profile\":{\"tierSourceProvider\":\"p\",\"tierOrder\":[\"large\",\"small\"],"
    "\"tierFallback\":[\"large\"],\"tierRegex\":\"^m-([a-z]+)\"},\"models\":{}}";

static const char TIERS_ANSWER[] = "{\"tiers\":[\"large\"]}";

int main(void) {
    uint8_t *out = NULL;
    size_t out_len = 0;
    int32_t status = forebay_library_routing_tiers((const uint8_t *)TIERS_CALL, strlen(TIERS_CALL), &out, &out_len);
    int failed = status != 0 || out == NULL || out_len != strlen(TIERS_ANSWER) ||
                 memcmp(out, TIERS_ANSWER, out_len) != 0;
    if (failed) {
        fprintf(stderr, "FAIL release: tiers answered status %d with %.*s\n", (int)status, (int)out_len,
                out == NULL ? "" : (const char *)out);
    }
    forebay_library_release(out);
    forebay_library_release(NULL);
    if (failed) {
        return 1;
    }
    printf("release_test: all checks passed\n");
    return 0;
}
