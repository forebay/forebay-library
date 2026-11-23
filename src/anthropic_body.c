#include <string.h>

#include "anthropic_operations.h"
#include "spisor/allocation.h"
#include "spisor/json_write.h"

/** Fills the answer with the JSON of a wire object, which it deletes; the answer is left empty unless OK. */
static spisor_status write_body(cJSON *wire, forebay_anthropic_body_answer *answer) {
    char *text = spisor_json_stringify(wire);
    cJSON_Delete(wire);
    if (text == NULL) {
        return SPISOR_OUT_OF_MEMORY;
    }
    size_t length = strlen(text);
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

spisor_status anthropic_finish_body(const anthropic_writer *writer, cJSON *wire, forebay_anthropic_body_answer *answer,
                                    spisor_refusal *refusal) {
    if (writer->status == SPISOR_OK) {
        return write_body(wire, answer);
    }
    cJSON_Delete(wire);
    if (writer->status != SPISOR_MALFORMED) {
        return writer->status;
    }
    anthropic_problem problem;
    anthropic_writer_problem(writer, &problem);
    return anthropic_refuse_problem(refusal, &problem);
}

spisor_status anthropic_finish_reply(const anthropic_writer *writer, cJSON *wire, uint32_t status,
                                     forebay_anthropic_body_answer *answer, spisor_refusal *refusal) {
    spisor_status finished = anthropic_finish_body(writer, wire, answer, refusal);
    if (finished == SPISOR_OK) {
        anthropic_head_body(answer, status, ANTHROPIC_JSON);
    }
    return finished;
}
