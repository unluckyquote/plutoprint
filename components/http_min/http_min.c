// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#include "http_min.h"

#include <limits.h>
#include <string.h>

enum {
    CHUNK_SIZE = 0,
    CHUNK_EXTENSION,
    CHUNK_SIZE_CR,
    CHUNK_DATA,
    CHUNK_DATA_CR,
    CHUNK_DATA_LF,
    CHUNK_TRAILER,
    CHUNK_TRAILER_CR,
    CHUNK_DONE,
    CHUNK_ERROR
};

static int lower_ascii(int c)
{
    if (c >= 'A' && c <= 'Z') {
        return c + ('a' - 'A');
    }
    return c;
}

static int text_equal_ci(const char *a, size_t len, const char *b)
{
    size_t i;

    for (i = 0; b[i] != '\0'; ++i) {
        if (i == len || lower_ascii((unsigned char)a[i]) != b[i]) {
            return 0;
        }
    }
    return i == len;
}

static const char *skip_spaces(const char *p, const char *end)
{
    while (p < end && (*p == ' ' || *p == '\t')) {
        ++p;
    }
    return p;
}

static const char *trim_end(const char *start, const char *end)
{
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) {
        --end;
    }
    return end;
}

int http_parse_head(const char *buf, size_t len, http_head_t *out)
{
    size_t end = 0;
    size_t line_start;
    const char *line_end;
    const char *p;
    const char *q;
    size_t method_len;
    size_t path_len;

    if (buf == NULL || out == NULL) {
        return -1;
    }
    for (end = 0; end + 3 < len; ++end) {
        if (buf[end] == '\r' && buf[end + 1] == '\n' &&
            buf[end + 2] == '\r' && buf[end + 3] == '\n') {
            break;
        }
    }
    if (end + 3 >= len) {
        return 0;
    }

    memset(out, 0, sizeof(*out));
    line_end = memchr(buf, '\n', end);
    if (line_end == NULL || line_end == buf || line_end[-1] != '\r') {
        return -1;
    }
    p = buf;
    q = memchr(p, ' ', (size_t)(line_end - p));
    if (q == NULL) {
        return -1;
    }
    method_len = (size_t)(q - p);
    if (method_len == 0 || method_len >= sizeof(out->method)) {
        return -1;
    }
    memcpy(out->method, p, method_len);
    out->method[method_len] = '\0';
    p = q + 1;
    q = memchr(p, ' ', (size_t)(line_end - p));
    if (q == NULL) {
        return -1;
    }
    path_len = (size_t)(q - p);
    if (path_len == 0 || path_len >= sizeof(out->path) ||
        (size_t)(line_end - q - 1) != 9 ||
        memcmp(q + 1, "HTTP/1.1\r", 9) != 0) {
        return -1;
    }
    memcpy(out->path, p, path_len);
    out->path[path_len] = '\0';

    line_start = (size_t)(line_end - buf) + 1;
    while (line_start <= end) {
        size_t colon;
        size_t next;
        const char *value_start;
        const char *value_end;
        const char *name_end;

        next = line_start;
        while (next <= end + 1 && buf[next] != '\n') {
            ++next;
        }
        if (next > end + 1 || next == line_start || buf[next - 1] != '\r') {
            return -1;
        }
        colon = line_start;
        while (colon < next && buf[colon] != ':') {
            ++colon;
        }
        if (colon == line_start || colon >= next) {
            return -1;
        }
        name_end = trim_end(buf + line_start, buf + colon);
        if (name_end == buf + line_start) {
            return -1;
        }
        value_start = skip_spaces(buf + colon + 1, buf + next - 1);
        value_end = trim_end(value_start, buf + next - 1);
        if (text_equal_ci(buf + line_start, (size_t)(name_end - (buf + line_start)),
                          "content-length")) {
            size_t value = 0;
            const char *v = value_start;

            if (v == value_end) {
                return -1;
            }
            while (v < value_end) {
                if (*v < '0' || *v > '9' ||
                    value > (SIZE_MAX - (size_t)(*v - '0')) / 10) {
                    return -1;
                }
                value = value * 10 + (size_t)(*v - '0');
                ++v;
            }
            out->content_length = value;
        } else if (text_equal_ci(buf + line_start,
                                 (size_t)(name_end - (buf + line_start)),
                                 "transfer-encoding")) {
            if (text_equal_ci(value_start, (size_t)(value_end - value_start),
                              "chunked")) {
                out->chunked = true;
            }
        } else if (text_equal_ci(buf + line_start,
                                 (size_t)(name_end - (buf + line_start)),
                                 "expect")) {
            if (text_equal_ci(value_start, (size_t)(value_end - value_start),
                              "100-continue")) {
                out->expect_continue = true;
            }
        }
        line_start = next + 1;
    }
    return (int)(end + 4);
}

void chunk_dec_init(chunk_dec_t *d)
{
    memset(d, 0, sizeof(*d));
    d->state = CHUNK_SIZE;
}

static int hex_value(uint8_t c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int chunk_dec_feed(chunk_dec_t *d, const uint8_t *in, size_t in_len,
                   uint8_t *out, size_t *consumed)
{
    size_t i;
    int produced = 0;

    if (d == NULL || consumed == NULL || (in_len != 0 && in == NULL)) {
        return -1;
    }
    *consumed = 0;
    for (i = 0; i < in_len; ++i) {
        uint8_t c = in[i];
        int digit;

        if (d->state == CHUNK_ERROR || d->state == CHUNK_DONE) {
            d->state = CHUNK_ERROR;
            *consumed = i;
            return -1;
        }
        switch (d->state) {
        case CHUNK_SIZE:
            digit = hex_value(c);
            if (digit >= 0) {
                if (d->size > (SIZE_MAX - (size_t)digit) / 16) {
                    d->state = CHUNK_ERROR;
                    *consumed = i + 1;
                    return -1;
                }
                d->size = d->size * 16 + (size_t)digit;
                d->have_size_digit = true;
            } else if (c == ';' && d->have_size_digit) {
                d->state = CHUNK_EXTENSION;
            } else if (c == '\r' && d->have_size_digit) {
                d->state = CHUNK_SIZE_CR;
            } else {
                d->state = CHUNK_ERROR;
                *consumed = i + 1;
                return -1;
            }
            break;
        case CHUNK_EXTENSION:
            if (c == '\r') d->state = CHUNK_SIZE_CR;
            else if (c == '\n') {
                d->state = CHUNK_ERROR;
                *consumed = i + 1;
                return -1;
            }
            break;
        case CHUNK_SIZE_CR:
            if (c != '\n') {
                d->state = CHUNK_ERROR;
                *consumed = i + 1;
                return -1;
            }
            d->remaining = d->size;
            d->size = 0;
            d->have_size_digit = false;
            d->state = d->remaining == 0 ? CHUNK_TRAILER : CHUNK_DATA;
            break;
        case CHUNK_DATA:
            out[produced++] = c;
            if (--d->remaining == 0) d->state = CHUNK_DATA_CR;
            break;
        case CHUNK_DATA_CR:
            if (c != '\r') d->state = CHUNK_ERROR;
            else d->state = CHUNK_DATA_LF;
            break;
        case CHUNK_DATA_LF:
            if (c != '\n') d->state = CHUNK_ERROR;
            else d->state = CHUNK_SIZE;
            break;
        case CHUNK_TRAILER:
            if (c == '\r') d->state = CHUNK_TRAILER_CR;
            else if (c == '\n') d->state = CHUNK_ERROR;
            else d->trailer_data = true;
            break;
        case CHUNK_TRAILER_CR:
            if (c != '\n') d->state = CHUNK_ERROR;
            else if (!d->trailer_data) d->state = CHUNK_DONE;
            else {
                d->trailer_data = false;
                d->state = CHUNK_TRAILER;
            }
            break;
        default:
            d->state = CHUNK_ERROR;
            break;
        }
        if (d->state == CHUNK_ERROR) {
            *consumed = i + 1;
            return -1;
        }
        *consumed = i + 1;
    }
    return produced;
}

bool chunk_dec_done(const chunk_dec_t *d)
{
    return d != NULL && d->state == CHUNK_DONE;
}
