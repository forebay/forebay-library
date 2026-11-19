#include <stdint.h>

#include "ir_json_reader.h"
#include "spisor/allocation.h"
#include "spisor/json_value.h"
#include "spisor/value.h"

/** One allocation, its bytes following the header; `tree` is filled only for a chunk that holds a value tree. */
struct ir_json_chunk {
    ir_json_chunk *next;
    spisor_value_tree tree;
};

/** A chunk header padded so the bytes after it are aligned for any type. */
typedef union chunk_header {
    ir_json_chunk chunk;
    long double long_double_alignment;
    uint64_t integer_alignment;
    void *pointer_alignment;
} chunk_header;

static ir_json_chunk *take_chunk(ir_json_arena *arena, size_t bytes) {
    if (bytes > SIZE_MAX - sizeof(chunk_header)) {
        return NULL;
    }
    chunk_header *header = spisor_memory_allocate_zeroed(1, sizeof(chunk_header) + bytes);
    if (header == NULL) {
        return NULL;
    }
    header->chunk.tree.size = (uint32_t)sizeof(spisor_value_tree);
    header->chunk.next = arena->chunks;
    arena->chunks = &header->chunk;
    return &header->chunk;
}

void *ir_json_arena_take(ir_json_arena *arena, size_t bytes) {
    ir_json_chunk *chunk = take_chunk(arena, bytes);
    return chunk == NULL ? NULL : (char *)chunk + sizeof(chunk_header);
}

const spisor_value *ir_json_arena_value(ir_json_arena *arena, const cJSON *json, spisor_status *status) {
    ir_json_chunk *chunk = take_chunk(arena, 0);
    if (chunk == NULL) {
        *status = SPISOR_OUT_OF_MEMORY;
        return NULL;
    }
    *status = spisor_value_of_json(json, &chunk->tree);
    return *status == SPISOR_OK ? &chunk->tree.root : NULL;
}

void ir_json_arena_release(ir_json_arena *arena) {
    if (arena == NULL) {
        return;
    }
    while (arena->chunks != NULL) {
        ir_json_chunk *next = arena->chunks->next;
        spisor_value_release(&arena->chunks->tree);
        spisor_memory_release(arena->chunks);
        arena->chunks = next;
    }
}
