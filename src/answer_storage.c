#include "answer_storage.h"

#include <stdint.h>
#include <string.h>

#include "spisor/allocation.h"

/** @implNote Spelled out rather than max_align_t, which MSVC's C mode does not declare. */
typedef union block_header {
    void *next;
    long double floating;
    long long integer;
    void (*function)(void);
} block_header;

void *forebay_storage_allocate(void **storage, size_t bytes) {
    if (bytes > SIZE_MAX - sizeof(block_header)) {
        return NULL;
    }
    block_header *block = spisor_memory_allocate_zeroed(1, sizeof(block_header) + bytes);
    if (block == NULL) {
        return NULL;
    }
    block->next = *storage;
    *storage = block;
    return block + 1;
}

void *forebay_storage_array(void **storage, size_t count, size_t size) {
    if (count == 0 || count > SIZE_MAX / size) {
        return NULL;
    }
    return forebay_storage_allocate(storage, count * size);
}

int forebay_storage_text(void **storage, const char *text, const char **copy) {
    *copy = NULL;
    if (text == NULL) {
        return 1;
    }
    size_t length = strlen(text);
    char *block = forebay_storage_allocate(storage, length + 1);
    if (block == NULL) {
        return 0;
    }
    memcpy(block, text, length + 1);
    *copy = block;
    return 1;
}

void forebay_storage_release(void *storage) {
    block_header *block = storage;
    while (block != NULL) {
        block_header *next = block->next;
        spisor_memory_release(block);
        block = next;
    }
}
