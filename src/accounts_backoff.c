#include "accounts_backoff.h"

#include <math.h>

double accounts_backoff_milliseconds(uint32_t attempt, double base, double maximum, double random) {
    double grown = base * pow(2, (double)attempt);
    double raw = fmin(maximum, isnan(grown) ? 0 : floor(grown));
    return floor(raw / 2.0 + random * (raw / 2.0));
}
