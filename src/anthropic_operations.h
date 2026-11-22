#ifndef ANTHROPIC_OPERATIONS_H
#define ANTHROPIC_OPERATIONS_H

#include "anthropic_check.h"
#include "anthropic_writer.h"
#include "answer_storage.h"
#include "forebay/anthropic.h"
#include "spisor/definition.h"

/** The operations of the anthropic definition, and what they share: shapes, refusals and releases. */

#define ANTHROPIC_BODY_ANSWER_SHAPE SPISOR_SHAPE_THROUGH(forebay_anthropic_body_answer, storage)

/** The release of an answer held in a storage chain, doing nothing for NULL or an answer short of its shape. */
#define ANTHROPIC_RELEASE_ANSWER(name, type, shape)                    \
    void name(void *context, type *answer) {                           \
        (void)context;                                                 \
        if (answer != NULL && spisor_has_shape(answer->size, shape)) { \
            forebay_storage_release(answer->storage);                  \
            SPISOR_CLEAR_ANSWER(answer, shape);                        \
        }                                                              \
    }

/** The wire object of an Anthropic error, NULL once the writer has failed. */
cJSON *anthropic_error_object(anthropic_writer *writer, const char *error_type, const char *message);

/** The MALFORMED refusal of the first rule an IR value breaks. */
spisor_status anthropic_refuse_problem(spisor_refusal *refusal, const anthropic_problem *problem);

/** The problem of a writer that failed MALFORMED: the value tree it names is not well formed. */
void anthropic_writer_problem(const anthropic_writer *writer, anthropic_problem *problem);

/** The MALFORMED refusal of bytes NULL beside a length that is not 0. */
spisor_status anthropic_refuse_bytes(spisor_refusal *refusal);

/**
 * Fills the answer with the JSON of a wire object the writer built, which it deletes, or answers what the writer
 * failed with: MALFORMED with the refusal of the problem it found, or OUT_OF_MEMORY. The answer is left empty unless
 * OK.
 */
spisor_status anthropic_finish_body(const anthropic_writer *writer, cJSON *wire, forebay_anthropic_body_answer *answer,
                                    spisor_refusal *refusal);

void anthropic_release_body(void *context, forebay_anthropic_body_answer *answer);

spisor_status anthropic_stream_decoder_open(void *context, forebay_anthropic_stream_decoder **decoder,
                                            spisor_refusal *refusal);
spisor_status anthropic_stream_decode(void *context, const forebay_anthropic_stream_decode_argument *argument,
                                      forebay_anthropic_events_answer *answer, spisor_refusal *refusal);
spisor_status anthropic_stream_decode_finish(void *context, const forebay_anthropic_stream_decode_argument *argument,
                                             forebay_anthropic_events_answer *answer, spisor_refusal *refusal);
void anthropic_release_events(void *context, forebay_anthropic_events_answer *answer);
void anthropic_release_stream_decoder(void *context, forebay_anthropic_stream_decoder *decoder);

spisor_status anthropic_stream_encoder_open(void *context, forebay_anthropic_stream_encoder **encoder,
                                            spisor_refusal *refusal);
spisor_status anthropic_stream_encode(void *context, const forebay_anthropic_stream_encode_argument *argument,
                                      forebay_anthropic_body_answer *answer, spisor_refusal *refusal);
spisor_status anthropic_stream_encode_finish(void *context, const forebay_anthropic_stream_encode_argument *argument,
                                             forebay_anthropic_body_answer *answer, spisor_refusal *refusal);
void anthropic_release_stream_encoder(void *context, forebay_anthropic_stream_encoder *encoder);

spisor_status anthropic_error_envelope(void *context, const forebay_anthropic_error_argument *argument,
                                       forebay_anthropic_body_answer *answer, spisor_refusal *refusal);
spisor_status anthropic_model_listing(void *context, const forebay_anthropic_model_listing_argument *argument,
                                      forebay_anthropic_body_answer *answer, spisor_refusal *refusal);
spisor_status anthropic_model_lookup(void *context, const forebay_anthropic_model_lookup_argument *argument,
                                     forebay_anthropic_model_lookup_answer *answer, spisor_refusal *refusal);
void anthropic_release_model_lookup(void *context, forebay_anthropic_model_lookup_answer *answer);

#endif
