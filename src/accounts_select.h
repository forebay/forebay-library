#ifndef ACCOUNTS_SELECT_H
#define ACCOUNTS_SELECT_H

#include "service_support.h"

SERVICE_EXPORT int32_t forebay_library_accounts_acquire(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);

#endif
