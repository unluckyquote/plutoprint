// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#ifndef HTTP_MIN_H
#define HTTP_MIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char method[8];
    char path[128];
    size_t content_length;
    bool chunked;
    bool expect_continue;
} http_head_t;

int http_parse_head(const char *buf, size_t len, http_head_t *out);

typedef struct {
    unsigned int state;
    size_t remaining;
    size_t size;
    bool have_size_digit;
    bool trailer_data;
} chunk_dec_t;

void chunk_dec_init(chunk_dec_t *d);
int chunk_dec_feed(chunk_dec_t *d, const uint8_t *in, size_t in_len,
                   uint8_t *out, size_t *consumed);
bool chunk_dec_done(const chunk_dec_t *d);

#endif
