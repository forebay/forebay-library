#ifndef ROUTING_TIERS_H
#define ROUTING_TIERS_H

#include "routing_support.h"

/**
 * The tier names the tier-source provider's ranking yields through tierRegex group 1, ordered by
 * tierOrder with unknown tiers last and alphabetical within a rank, or tierFallback when none match.
 * Answers NULL with refusal set when tierRegex is unusable, or with it clear when memory runs out.
 */
cJSON *routing_detect_tiers(const routing_profile *profile, const cJSON *models, routing_refusal *refusal);

SERVICE_EXPORT int32_t routing_tiers(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);

#endif
