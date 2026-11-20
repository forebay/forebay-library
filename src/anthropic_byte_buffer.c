#include "anthropic_byte_buffer.h"

#include <string.h>

#include "spisor/allocation.h"

int anthropic_byte_buffer_reserve(anthropic_byte_buffer *buffer, size_t needed) {
    if (needed <= buffer->capacity) {
        return 1;
    }
    size_t doubled = buffer->capacity > SIZE_MAX / 2 ? SIZE_MAX : buffer->capacity * 2;
    size_t capacity = doubled > needed ? doubled : needed;
    uint8_t *grown = spisor_memory_reallocate(buffer->bytes, capacity);
    if (grown == NULL) {
        return 0;
    }
    buffer->bytes = grown;
    buffer->capacity = capacity;
    return 1;
}

int anthropic_byte_buffer_append(anthropic_byte_buffer *buffer, const void *bytes, size_t length) {
    if (length > SIZE_MAX - buffer->length || !anthropic_byte_buffer_reserve(buffer, buffer->length + length)) {
        return 0;
    }
    if (length > 0) {
        memcpy(buffer->bytes + buffer->length, bytes, length);
    }
    buffer->length += length;
    return 1;
}
