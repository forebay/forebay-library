#ifndef ACCOUNTS_BACKOFF_H
#define ACCOUNTS_BACKOFF_H

/**
 * RateLimitMath.calculateBackoffMs with jitter: floor(raw / 2 + random * raw / 2) where
 * raw = min(maximum, floor(base * 2^attempt)) and a negative attempt counts as 0.
 *
 * @implNote Java's long cast turns a NaN product (a zero base times an infinite power) into 0, so
 * a zero base backs off by nothing however large the attempt.
 */
double accounts_backoff_milliseconds(double attempt, double base, double maximum, double random);

#endif
