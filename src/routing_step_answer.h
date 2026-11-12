#ifndef ROUTING_STEP_ANSWER_H
#define ROUTING_STEP_ANSWER_H

#include "routing_step_walk.h"

/** {kind}, or NULL when memory runs out, as every builder here answers. */
cJSON *routing_kind_step(const char *kind);
cJSON *routing_reply_step(const char *kind, int status, const char *message);
cJSON *routing_end_stream_step(const char *message);
cJSON *routing_call_provider_step(const step_walk *walk);

/** A providerFailure reply carrying the failure's headers fit to forward under a rewritten body. */
cJSON *routing_provider_failure_step(double status, const cJSON *headers);

/** A rateLimited reply carrying the walk's last limited headers and its latest reset. */
cJSON *routing_rate_limited_step(const step_walk *walk);

/** {step, walk?, log} with step consumed and the log taken out of the walk; the walk goes along when a report is expected. */
cJSON *routing_answer_of(step_walk *walk, cJSON *step, int expects_report);

#endif
