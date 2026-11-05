#ifndef ACCOUNTS_TOKEN_H
#define ACCOUNTS_TOKEN_H

#include "service_support.h"

SERVICE_EXPORT int32_t accounts_token_expired(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);

#endif
