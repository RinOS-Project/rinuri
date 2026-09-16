/* SPDX-License-Identifier: MIT */
#ifndef RINURI_URI_H
#define RINURI_URI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RIN_URI_MAX_BYTES ((size_t)4096u)

typedef enum RinUriStatus {
    RIN_URI_OK = 0,
    RIN_URI_INVALID_ARGUMENT = -1,
    RIN_URI_MALFORMED = -2,
    RIN_URI_TOO_LONG = -3,
    RIN_URI_BUFFER_TOO_SMALL = -4,
    RIN_URI_OVERFLOW = -5
} RinUriStatus;

typedef struct RinUriSpan {
    const char* data;
    size_t size;
} RinUriSpan;

typedef enum RinUriHostKind {
    RIN_URI_HOST_NONE = 0,
    RIN_URI_HOST_REG_NAME = 1,
    RIN_URI_HOST_IPV4 = 2,
    RIN_URI_HOST_IPV6 = 3
} RinUriHostKind;

/* A parsed URI is a non-owning view into the caller's input. Every span is
 * bounded and need not be NUL terminated. Empty components have a non-NULL
 * data pointer when the corresponding delimiter was present. */
typedef struct RinUri {
    const char* input;
    size_t input_size;
    RinUriSpan scheme;
    RinUriSpan username;
    RinUriSpan password;
    RinUriSpan host;
    RinUriSpan port;
    RinUriSpan path;
    RinUriSpan query;
    RinUriSpan fragment;
    uint16_t port_number;
    uint8_t has_authority;
    uint8_t has_userinfo;
    uint8_t has_password;
    uint8_t has_query;
    uint8_t has_fragment;
    uint8_t is_absolute;
    RinUriHostKind host_kind;
} RinUri;

/* Parse an RFC 3986-shaped URI without allocation. Raw non-ASCII bytes are
 * rejected; callers can percent-encode UTF-8 before parsing. Percent escapes
 * are validated but intentionally not decoded by this API. */
int rin_uri_parse(const char* input, size_t input_size, RinUri* output);

/* Case-insensitive comparisons for the ASCII scheme and host components. */
int rin_uri_scheme_is(const RinUri* uri, const char* expected);
int rin_uri_host_is(const RinUri* uri, const char* expected);

/* Apply RinOS's stable normalization policy: lowercase scheme/host, collapse
 * separators and remove dot segments from the path, while preserving percent
 * escapes and query/fragment bytes. The result is NUL terminated; output is
 * empty on failure when it has writable capacity. */
int rin_uri_normalize(const char* input, size_t input_size, char* output,
                      size_t output_capacity, size_t* output_size);

#ifdef __cplusplus
}
#endif

#endif
