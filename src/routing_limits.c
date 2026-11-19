#include "routing_limits.h"

#define TOO_LARGE_STATUS 413u
#define MODELS_BUDGET_MILLISECONDS 2000u
#define REDERIVE_INTERVAL_MILLISECONDS 3000u
#define REFRESH_EVERY_MILLISECONDS (60u * 60u * 1000u)

/** Set above what any upstream accepts in one request, so the cap never refuses what an upstream would serve. */
#define MAX_REQUEST_BYTES (64u * 1024u * 1024u)
#define MAX_REQUEST_BYTES_TEXT "67108864"

void routing_read_limits(forebay_limits_answer *answer) {
    answer->max_request_bytes = MAX_REQUEST_BYTES;
    answer->too_large_status = TOO_LARGE_STATUS;
    answer->too_large_message =
        "the request body is larger than the " MAX_REQUEST_BYTES_TEXT " bytes the proxy accepts";
    answer->models_budget_milliseconds = MODELS_BUDGET_MILLISECONDS;
    answer->rederive_interval_milliseconds = REDERIVE_INTERVAL_MILLISECONDS;
    answer->refresh_every_milliseconds = REFRESH_EVERY_MILLISECONDS;
}
