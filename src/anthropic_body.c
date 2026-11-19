#include <string.h>

#include "anthropic_operations.h"
#include "anthropic_stringify.h"
#include "spisor/allocation.h"

spisor_status anthropic_write_body(cJSON *wire, forebay_anthropic_body_answer *answer) {
    size_t length = 0;
    char *text = anthropic_stringify(wire, &length);
    cJSON_Delete(wire);
    if (text == NULL) {
        return SPISOR_OUT_OF_MEMORY;
    }
    void *storage = NULL;
    char *body = forebay_storage_allocate(&storage, length + 1);
    if (body != NULL) {
        memcpy(body, text, length + 1);
        answer->bytes = (const uint8_t *)body;
        answer->length = length;
        answer->storage = storage;
    }
    spisor_memory_release(text);
    return body == NULL ? SPISOR_OUT_OF_MEMORY : SPISOR_OK;
}
