#include "accounts_backoff.h"

#include <math.h>

double accounts_backoff_milliseconds(double attempt, double base, double maximum, double random) {
    double steps = attempt > 0 ? floor(attempt) : 0;
    double grown = base * pow(2, steps);
    double raw = fmin(maximum, isnan(grown) ? 0 : floor(grown));
    return floor(raw / 2.0 + random * (raw / 2.0));
}
