#ifndef ACCOUNTS_MANAGE_H
#define ACCOUNTS_MANAGE_H

#include "service_support.h"

SERVICE_EXPORT int32_t accounts_list(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);
SERVICE_EXPORT int32_t accounts_upsert(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);
SERVICE_EXPORT int32_t accounts_remove(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);

#endif
