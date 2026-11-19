#ifndef ACCOUNTS_BACKOFF_H
#define ACCOUNTS_BACKOFF_H

#include <stdint.h>

/**
 * RateLimitMath.calculateBackoffMs with jitter: floor(raw / 2 + random * raw / 2) where
 * raw = min(maximum, floor(base * 2^attempt)).
 *
 * @implNote Java's long cast turns a NaN product (a zero base times an infinite power) into 0, so
 * a zero base backs off by nothing however large the attempt.
 */
double accounts_backoff_milliseconds(uint32_t attempt, double base, double maximum, double random);

#endif
