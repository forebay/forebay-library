#ifndef FOREBAY_PUBLIC_REPLY_H
#define FOREBAY_PUBLIC_REPLY_H

/** @file */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The reply the proxy answers a client with, whichever service decided it. Append-only: a published value keeps its
 * number.
 */
typedef enum forebay_reply_kind {
    FOREBAY_REPLY_PROXY_ERROR = 1,
    FOREBAY_REPLY_API_ERROR = 2,
    FOREBAY_REPLY_PROVIDER_FAILURE = 3,
    FOREBAY_REPLY_RATE_LIMITED = 4,
    FOREBAY_REPLY_INVALID_REQUEST = 5
} forebay_reply_kind;

#ifdef __cplusplus
}
#endif

#endif
