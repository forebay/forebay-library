#ifndef ANTHROPIC_FORWARDED_HEADERS_H
#define ANTHROPIC_FORWARDED_HEADERS_H

#include <stddef.h>

#include "forebay/pair.h"

/**
 * The client's headers a provider may send on to the upstream, in `*storage`: names lower cased, the credentials a
 * provider replaces, the headers the transport sets itself and the hop by hop headers left out, a later header with a
 * name already given replacing the earlier one's value.
 *
 * @return 1, with `*forwarded` NULL beside a `*count` of 0 when none is kept; 0 when memory runs out.
 */
int anthropic_forward_headers(void **storage, const forebay_pair *headers, size_t count,
                              const forebay_pair **forwarded, size_t *forwarded_count);

#endif
