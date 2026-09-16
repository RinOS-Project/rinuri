/* SPDX-License-Identifier: MIT */
#include "include/rinuri/uri.h"

#include <limits.h>

static int uri_alpha(unsigned char value)
{
    return (value >= (unsigned char)'A' && value <= (unsigned char)'Z') ||
           (value >= (unsigned char)'a' && value <= (unsigned char)'z');
}

static int uri_digit(unsigned char value)
{
    return value >= (unsigned char)'0' && value <= (unsigned char)'9';
}

static int uri_hex(unsigned char value)
{
    return uri_digit(value) ||
           (value >= (unsigned char)'A' && value <= (unsigned char)'F') ||
           (value >= (unsigned char)'a' && value <= (unsigned char)'f');
}

static int uri_unreserved(unsigned char value)
{
    return uri_alpha(value) || uri_digit(value) || value == (unsigned char)'-' ||
           value == (unsigned char)'.' || value == (unsigned char)'_' ||
           value == (unsigned char)'~';
}

static int uri_subdelim(unsigned char value)
{
    return value == (unsigned char)'!' || value == (unsigned char)'$' ||
           value == (unsigned char)'&' || value == (unsigned char)('\'') ||
           value == (unsigned char)'(' || value == (unsigned char)')' ||
           value == (unsigned char)'*' || value == (unsigned char)'+' ||
           value == (unsigned char)',' || value == (unsigned char)';' ||
           value == (unsigned char)'=';
}

static int uri_pct_or(int value, int allow_colon, int allow_at,
                      int allow_question, int allow_slash)
{
    const unsigned char byte = (unsigned char)value;
    return uri_unreserved(byte) || uri_subdelim(byte) ||
           (allow_colon && byte == (unsigned char)':') ||
           (allow_at && byte == (unsigned char)'@') ||
           (allow_question && byte == (unsigned char)'?') ||
           byte == (unsigned char)'%' ||
           (allow_slash && byte == (unsigned char)'/');
}

static int uri_component_valid(const char* value, size_t size,
                               int allow_colon, int allow_at,
                               int allow_question, int allow_slash)
{
    size_t index;
    if (size != 0u && value == NULL) return 0;
    for (index = 0u; index < size; ++index) {
        const unsigned char byte = (unsigned char)value[index];
        if (byte == 0u || byte >= 0x80u || byte <= 0x20u || byte == 0x7fu)
            return 0;
        if (byte == (unsigned char)'%') {
            if (index + 2u >= size ||
                !uri_hex((unsigned char)value[index + 1u]) ||
                !uri_hex((unsigned char)value[index + 2u]))
                return 0;
            index += 2u;
            continue;
        }
        if (!uri_pct_or(byte, allow_colon, allow_at, allow_question,
                        allow_slash))
            return 0;
    }
    return 1;
}

static int uri_span_equal_ci(RinUriSpan span, const char* expected)
{
    size_t index = 0u;
    if (!expected || (span.size != 0u && !span.data)) return 0;
    while (expected[index] != '\0') {
        unsigned char left;
        unsigned char right;
        if (index >= span.size) return 0;
        left = (unsigned char)span.data[index];
        right = (unsigned char)expected[index];
        if (left >= (unsigned char)'A' && left <= (unsigned char)'Z')
            left = (unsigned char)(left + ((unsigned char)'a' - (unsigned char)'A'));
        if (right >= (unsigned char)'A' && right <= (unsigned char)'Z')
            right = (unsigned char)(right + ((unsigned char)'a' - (unsigned char)'A'));
        if (left != right) return 0;
        ++index;
    }
    return index == span.size;
}

static int uri_ipv4_valid(const char* value, size_t size)
{
    size_t index = 0u;
    unsigned parts = 0u;
    if (!value || size == 0u) return 0;
    while (index < size) {
        size_t begin = index;
        unsigned number = 0u;
        unsigned digits = 0u;
        while (index < size && uri_digit((unsigned char)value[index])) {
            if (++digits > 3u) return 0;
            number = number * 10u + (unsigned)(value[index] - '0');
            ++index;
        }
        if (digits == 0u || number > 255u ||
            (digits > 1u && value[begin] == '0')) return 0;
        ++parts;
        if (index == size) break;
        if (value[index++] != '.' || index == size) return 0;
    }
    return parts == 4u;
}

static int uri_ipv6_valid(const char* value, size_t size)
{
    size_t index = 0u;
    unsigned groups = 0u;
    int compressed = 0;
    if (!value || size == 0u) return 0;
    if (value[0] == ':') {
        if (size < 2u || value[1] != ':') return 0;
        compressed = 1;
        index = 2u;
        if (index == size) return 1;
    }
    while (index < size) {
        unsigned digits = 0u;
        while (index < size && uri_hex((unsigned char)value[index])) {
            if (++digits > 4u) return 0;
            ++index;
        }
        if (digits == 0u) return 0;
        ++groups;
        if (index == size) break;
        if (value[index] != ':') return 0;
        if (index + 1u < size && value[index + 1u] == ':') {
            if (compressed) return 0;
            compressed = 1;
            index += 2u;
            if (index == size) break;
        } else {
            ++index;
            if (index == size) return 0;
        }
    }
    return compressed ? groups < 8u : groups == 8u;
}

static int uri_host_valid(const char* value, size_t size,
                          RinUriHostKind* kind_out)
{
    size_t index;
    int decimal = 1;
    int has_dot = 0;
    if (!value || size == 0u || !kind_out) return 0;
    for (index = 0u; index < size; ++index) {
        const unsigned char byte = (unsigned char)value[index];
        if (byte == (unsigned char)'.') has_dot = 1;
        if (byte == (unsigned char)'%') {
            if (index + 2u >= size ||
                !uri_hex((unsigned char)value[index + 1u]) ||
                !uri_hex((unsigned char)value[index + 2u]))
                return 0;
            decimal = 0;
            index += 2u;
            continue;
        }
        if (!uri_unreserved(byte) && !uri_subdelim(byte)) return 0;
        if (!uri_digit(byte) && byte != (unsigned char)'.') decimal = 0;
    }
    if (decimal && has_dot && uri_ipv4_valid(value, size)) {
        *kind_out = RIN_URI_HOST_IPV4;
    } else {
        *kind_out = RIN_URI_HOST_REG_NAME;
    }
    return 1;
}

static void uri_clear(RinUri* output)
{
    size_t index;
    unsigned char* bytes = (unsigned char*)output;
    for (index = 0u; index < sizeof(*output); ++index) bytes[index] = 0u;
}

int rin_uri_parse(const char* input, size_t input_size, RinUri* output)
{
    size_t index;
    size_t cursor = 0u;
    size_t authority_end;
    size_t first_delimiter = input_size;
    if (!output || !input) return RIN_URI_INVALID_ARGUMENT;
    uri_clear(output);
    if (input_size > RIN_URI_MAX_BYTES) return RIN_URI_TOO_LONG;
    output->input = input;
    output->input_size = input_size;
    for (index = 0u; index < input_size; ++index) {
        const unsigned char byte = (unsigned char)input[index];
        if (byte == 0u || byte <= 0x20u || byte >= 0x7fu) return RIN_URI_MALFORMED;
        if (byte == '/' || byte == '?' || byte == '#' || byte == ':') {
            first_delimiter = index;
            break;
        }
    }
    if (first_delimiter < input_size && input[first_delimiter] == ':') {
        if (first_delimiter == 0u || !uri_alpha((unsigned char)input[0]))
            return RIN_URI_MALFORMED;
        for (index = 1u; index < first_delimiter; ++index) {
            const unsigned char byte = (unsigned char)input[index];
            if (!uri_alpha(byte) && !uri_digit(byte) && byte != '+' &&
                byte != '-' && byte != '.') return RIN_URI_MALFORMED;
        }
        output->scheme.data = input;
        output->scheme.size = first_delimiter;
        output->is_absolute = 1u;
        cursor = first_delimiter + 1u;
    } else {
        /* A colon in the first relative path segment would be an invalid
         * scheme, rather than a path character. */
        for (index = 0u; index < first_delimiter; ++index)
            if (input[index] == ':') return RIN_URI_MALFORMED;
    }

    if (cursor + 1u < input_size && input[cursor] == '/' &&
        input[cursor + 1u] == '/') {
        size_t userinfo_end = cursor + 2u;
        size_t at_count = 0u;
        output->has_authority = 1u;
        cursor += 2u;
        authority_end = cursor;
        while (authority_end < input_size && input[authority_end] != '/' &&
               input[authority_end] != '?' && input[authority_end] != '#')
            ++authority_end;
        for (index = cursor; index < authority_end; ++index)
            if (input[index] == '@') {
                ++at_count;
                userinfo_end = index;
            }
        if (at_count > 1u) return RIN_URI_MALFORMED;
        if (at_count == 1u) {
            size_t colon = cursor;
            output->has_userinfo = 1u;
            if (userinfo_end == cursor) return RIN_URI_MALFORMED;
            while (colon < userinfo_end && input[colon] != ':') ++colon;
            output->username.data = input + cursor;
            output->username.size = colon - cursor;
            if (output->username.size == 0u ||
                !uri_component_valid(output->username.data, output->username.size,
                                     0, 0, 0, 0)) return RIN_URI_MALFORMED;
            if (colon < userinfo_end) {
                output->has_password = 1u;
                output->password.data = input + colon + 1u;
                output->password.size = userinfo_end - colon - 1u;
                if (!uri_component_valid(output->password.data,
                                         output->password.size, 0, 0, 0, 0))
                    return RIN_URI_MALFORMED;
            }
            cursor = userinfo_end + 1u;
        }
        if (cursor >= authority_end) return RIN_URI_MALFORMED;
        if (input[cursor] == '[') {
            size_t close = cursor + 1u;
            while (close < authority_end && input[close] != ']') ++close;
            if (close >= authority_end ||
                !uri_ipv6_valid(input + cursor + 1u, close - cursor - 1u))
                return RIN_URI_MALFORMED;
            output->host.data = input + cursor + 1u;
            output->host.size = close - cursor - 1u;
            output->host_kind = RIN_URI_HOST_IPV6;
            cursor = close + 1u;
            if (cursor < authority_end) {
                if (input[cursor] != ':' || cursor + 1u == authority_end)
                    return RIN_URI_MALFORMED;
                ++cursor;
                output->port.data = input + cursor;
                output->port.size = authority_end - cursor;
            }
        } else {
            size_t colon = cursor;
            while (colon < authority_end && input[colon] != ':') ++colon;
            output->host.data = input + cursor;
            output->host.size = colon - cursor;
            if (!uri_host_valid(output->host.data, output->host.size,
                                &output->host_kind)) return RIN_URI_MALFORMED;
            if (colon < authority_end) {
                if (colon + 1u == authority_end) return RIN_URI_MALFORMED;
                output->port.data = input + colon + 1u;
                output->port.size = authority_end - colon - 1u;
            }
        }
        if (output->port.size != 0u) {
            uint32_t port = 0u;
            if (output->port.size > 5u) return RIN_URI_MALFORMED;
            for (index = 0u; index < output->port.size; ++index) {
                if (!uri_digit((unsigned char)output->port.data[index]))
                    return RIN_URI_MALFORMED;
                port = port * 10u + (uint32_t)(output->port.data[index] - '0');
            }
            if (port > 65535u) return RIN_URI_MALFORMED;
            output->port_number = (uint16_t)port;
        }
        cursor = authority_end;
    }

    output->path.data = input + cursor;
    while (cursor < input_size && input[cursor] != '?' && input[cursor] != '#')
        ++cursor;
    output->path.size = cursor - (size_t)(output->path.data - input);
    if (!uri_component_valid(output->path.data, output->path.size, 1, 1, 0, 1))
        return RIN_URI_MALFORMED;
    if (cursor < input_size && input[cursor] == '?') {
        output->has_query = 1u;
        output->query.data = input + ++cursor;
        while (cursor < input_size && input[cursor] != '#') ++cursor;
        output->query.size = cursor - (size_t)(output->query.data - input);
        if (!uri_component_valid(output->query.data, output->query.size, 1, 1, 1, 0))
            return RIN_URI_MALFORMED;
    }
    if (cursor < input_size && input[cursor] == '#') {
        output->has_fragment = 1u;
        output->fragment.data = input + ++cursor;
        output->fragment.size = input_size - cursor;
        if (!uri_component_valid(output->fragment.data, output->fragment.size,
                                 1, 1, 1, 0)) return RIN_URI_MALFORMED;
    }
    return RIN_URI_OK;
}

int rin_uri_scheme_is(const RinUri* uri, const char* expected)
{
    return uri && uri_span_equal_ci(uri->scheme, expected);
}

int rin_uri_host_is(const RinUri* uri, const char* expected)
{
    return uri && uri_span_equal_ci(uri->host, expected);
}

static int uri_append(char* output, size_t capacity, size_t* written,
                      char value)
{
    if (*written + 1u >= capacity) return 0;
    output[(*written)++] = value;
    return 1;
}

static int uri_append_span(char* output, size_t capacity, size_t* written,
                           RinUriSpan span, int lowercase)
{
    size_t index;
    for (index = 0u; index < span.size; ++index) {
        unsigned char value = (unsigned char)span.data[index];
        if (lowercase && value >= (unsigned char)'A' &&
            value <= (unsigned char)'Z')
            value = (unsigned char)(value + ((unsigned char)'a' - (unsigned char)'A'));
        if (!uri_append(output, capacity, written, (char)value)) return 0;
    }
    return 1;
}

static int uri_append_normalized_port(char* output, size_t capacity,
                                      size_t* written, RinUriSpan port)
{
    size_t first = 0u;
    if (port.size == 0u) return 1;
    while (first + 1u < port.size && port.data[first] == '0') ++first;
    return uri_append_span(output, capacity, written,
                           (RinUriSpan){port.data + first, port.size - first},
                           0);
}

static int uri_append_normalized_path(char* output, size_t capacity,
                                      size_t* written, RinUriSpan path)
{
    size_t segment_starts[RIN_URI_MAX_BYTES / 2u + 1u];
    size_t segment_count = 0u;
    size_t index = 0u;
    size_t start = 0u;
    int absolute = path.size != 0u && path.data[0] == '/';
    int trailing_separator = path.size != 0u &&
                             (path.data[path.size - 1u] == '/' ||
                              path.data[path.size - 1u] == '.');
    if (absolute && !uri_append(output, capacity, written, '/')) return 0;
    while (index <= path.size) {
        if (index != path.size && path.data[index] != '/') {
            ++index;
            continue;
        }
        if (index > start) {
            const size_t component_size = index - start;
            RinUriSpan component = {path.data + start, component_size};
            if (component_size == 1u && component.data[0] == '.') {
                /* no-op */
            } else if (component_size == 2u && component.data[0] == '.' &&
                       component.data[1] == '.') {
                if (segment_count != 0u) {
                    *written = segment_starts[--segment_count];
                } else if (!absolute) {
                    if (*written != 0u && output[*written - 1u] != '/' &&
                        !uri_append(output, capacity, written, '/')) return 0;
                    segment_starts[segment_count++] = *written;
                    if (!uri_append_span(output, capacity, written, component, 0))
                        return 0;
                }
            } else {
                if (*written != 0u && output[*written - 1u] != '/' &&
                    !uri_append(output, capacity, written, '/')) return 0;
                if (segment_count >= sizeof(segment_starts) / sizeof(segment_starts[0]))
                    return 0;
                segment_starts[segment_count++] = *written;
                if (!uri_append_span(output, capacity, written, component, 0))
                    return 0;
            }
        }
        start = index + 1u;
        ++index;
    }
    if (trailing_separator && *written != 0u && output[*written - 1u] != '/' &&
        !uri_append(output, capacity, written, '/')) return 0;
    if (!absolute && path.size != 0u && *written == 0u &&
        !uri_append(output, capacity, written, '.')) return 0;
    return 1;
}

int rin_uri_normalize(const char* input, size_t input_size, char* output,
                      size_t output_capacity, size_t* output_size)
{
    RinUri uri;
    char scratch[RIN_URI_MAX_BYTES + 1u];
    size_t written = 0u;
    if (!output_size || !input || (output_capacity != 0u && !output))
        return RIN_URI_INVALID_ARGUMENT;
    *output_size = 0u;
    if (output != NULL && output_capacity != 0u) output[0] = '\0';
    if (rin_uri_parse(input, input_size, &uri) != RIN_URI_OK)
        return input_size > RIN_URI_MAX_BYTES ? RIN_URI_TOO_LONG : RIN_URI_MALFORMED;
    if (uri.scheme.size != 0u) {
        if (!uri_append_span(scratch, sizeof(scratch), &written, uri.scheme, 1) ||
            !uri_append(scratch, sizeof(scratch), &written, ':'))
            return RIN_URI_OVERFLOW;
    }
    if (uri.has_authority) {
        if (!uri_append(scratch, sizeof(scratch), &written, '/') ||
            !uri_append(scratch, sizeof(scratch), &written, '/'))
            return RIN_URI_OVERFLOW;
        if (uri.has_userinfo) {
            if (!uri_append_span(scratch, sizeof(scratch), &written,
                                 uri.username, 0)) return RIN_URI_OVERFLOW;
            if (uri.has_password) {
                if (!uri_append(scratch, sizeof(scratch), &written, ':') ||
                    !uri_append_span(scratch, sizeof(scratch), &written,
                                     uri.password, 0)) return RIN_URI_OVERFLOW;
            }
            if (!uri_append(scratch, sizeof(scratch), &written, '@'))
                return RIN_URI_OVERFLOW;
        }
        if (uri.host_kind == RIN_URI_HOST_IPV6) {
            if (!uri_append(scratch, sizeof(scratch), &written, '[') ||
                !uri_append_span(scratch, sizeof(scratch), &written, uri.host, 1) ||
                !uri_append(scratch, sizeof(scratch), &written, ']'))
                return RIN_URI_OVERFLOW;
        } else if (!uri_append_span(scratch, sizeof(scratch), &written,
                                    uri.host, 1)) return RIN_URI_OVERFLOW;
        if (uri.port.size != 0u) {
            if (!uri_append(scratch, sizeof(scratch), &written, ':') ||
                !uri_append_normalized_port(scratch, sizeof(scratch), &written,
                                            uri.port))
                return RIN_URI_OVERFLOW;
        }
    }
    if (!uri_append_normalized_path(scratch, sizeof(scratch), &written, uri.path))
        return RIN_URI_OVERFLOW;
    if (uri.has_query) {
        if (!uri_append(scratch, sizeof(scratch), &written, '?') ||
            !uri_append_span(scratch, sizeof(scratch), &written, uri.query, 0))
            return RIN_URI_OVERFLOW;
    }
    if (uri.has_fragment) {
        if (!uri_append(scratch, sizeof(scratch), &written, '#') ||
            !uri_append_span(scratch, sizeof(scratch), &written, uri.fragment, 0))
            return RIN_URI_OVERFLOW;
    }
    *output_size = written;
    if (output_capacity <= written) return RIN_URI_BUFFER_TOO_SMALL;
    {
        size_t index;
        for (index = 0u; index < written; ++index) output[index] = scratch[index];
        output[written] = '\0';
    }
    return RIN_URI_OK;
}
