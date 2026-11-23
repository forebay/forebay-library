#ifndef ROUTING_TIERS_H
#define ROUTING_TIERS_H

#include "routing_support.h"

/**
 * The tier names the tier-source provider's ranking yields through tier_regex group 1, ordered by
 * tierOrder with unknown tiers last and alphabetical within a rank, or tierFallback when none match or the
 * profile declares no tier_regex.
 * Answers NULL with refusal set when tier_regex is unusable, or with it clear when memory runs out.
 */
cJSON *routing_detect_tiers(const routing_profile *profile, const cJSON *models, routing_refusal *refusal);

spisor_status routing_tiers_call(const cJSON *argument, service_reply *reply);

#endif
