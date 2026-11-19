#ifndef ANTHROPIC_BYTE_BUFFER_H
#define ANTHROPIC_BYTE_BUFFER_H

#include <stddef.h>
#include <stdint.h>

/** Bytes that grow, from the binary's one allocator; all zero is empty, and spisor_memory_release frees `bytes`. */
typedef struct anthropic_byte_buffer {
    uint8_t *bytes;
    size_t length;
    size_t capacity;
} anthropic_byte_buffer;

/** Room for `needed` bytes in all, what it holds kept; 0 when memory runs out, the buffer unchanged. */
int anthropic_byte_buffer_reserve(anthropic_byte_buffer *buffer, size_t needed);

/** Appends `length` bytes; 0 when memory runs out or the length cannot be held, the buffer unchanged. */
int anthropic_byte_buffer_append(anthropic_byte_buffer *buffer, const void *bytes, size_t length);

#endif
