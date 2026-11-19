#ifndef ROUTING_LIMITS_H
#define ROUTING_LIMITS_H

#include "forebay/routing.h"

/**
 * The bounds every host applies to what a client sends its proxy, so a local process cannot grow a
 * host's memory without limit. It allocates nothing: every text it answers is static.
 */
void routing_read_limits(forebay_limits_answer *answer);

#endif
