// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#include "urf_testpage.h"

#include <string.h>

static const uint8_t urf_header[] = {
    'U', 'N', 'I', 'R', 'A', 'S', 'T', 0,
    0, 0, 0, 1,
    8, 0, 1, 4,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 9, 176, 0, 0, 13, 180, 0, 0, 1, 44,
    0, 0, 0, 0, 0, 0, 0, 0,
};

static int line_kind(size_t y)
{
    int frame = y >= 100 && y < URF_TESTPAGE_HEIGHT - 100;
    int horizontal = frame && (y < 103 || y >= URF_TESTPAGE_HEIGHT - 103);
    int square = y >= (URF_TESTPAGE_HEIGHT - 600) / 2 &&
                 y < (URF_TESTPAGE_HEIGHT + 600) / 2;

    return horizontal ? 1 : (frame ? (square ? 3 : 2) : (square ? 4 : 0));
}

static void make_line(uint8_t *line, size_t y)
{
    int kind = line_kind(y);

    memset(line, 255, URF_TESTPAGE_WIDTH);
    if (kind == 1) {
        memset(line + 100, 0, URF_TESTPAGE_WIDTH - 200);
    } else if (kind == 2 || kind == 3) {
        memset(line + 100, 0, 3);
        memset(line + URF_TESTPAGE_WIDTH - 103, 0, 3);
    }
    if (kind == 3 || kind == 4) {
        memset(line + (URF_TESTPAGE_WIDTH - 600) / 2, 0, 600);
    }
}

static size_t group_length(size_t y)
{
    int kind = line_kind(y);
    size_t count = 1;

    while (y + count < URF_TESTPAGE_HEIGHT && count < 256 &&
           line_kind(y + count) == kind) {
        ++count;
    }
    return count;
}

static void begin_packet(urf_testpage_t *s)
{
    size_t i = s->pack_pos;
    size_t j;

    s->packet_pos = 0;
    s->packet_pending = 1;
    if (i + 1 < URF_TESTPAGE_WIDTH && s->line[i + 1] == s->line[i]) {
        j = i + 1;
        while (j + 1 < URF_TESTPAGE_WIDTH && s->line[j + 1] == s->line[i] &&
               j - i < 127) {
            ++j;
        }
        s->packet_literal = 0;
        s->packet_len = 2;
        s->packet_pixels = j - i + 1;
        s->packet_source = i;
        s->packet_value = s->line[i];
    } else {
        j = i;
        while (j < URF_TESTPAGE_WIDTH && j - i < 128 &&
               (j + 1 >= URF_TESTPAGE_WIDTH || s->line[j + 1] != s->line[j])) {
            ++j;
        }
        if (j == i) {
            ++j;
        }
        s->packet_literal = 1;
        s->packet_len = 1 + j - i;
        s->packet_pixels = j - i;
        s->packet_source = i;
    }
}

static uint8_t packet_byte(const urf_testpage_t *s, size_t pos)
{
    if (pos == 0) {
        return s->packet_literal ? (uint8_t)(257 - (s->packet_len - 1))
                                 : (uint8_t)(s->packet_pixels - 1);
    }
    return s->packet_literal ? s->line[s->packet_source + pos - 1]
                             : s->packet_value;
}

void urf_testpage_init(urf_testpage_t *s)
{
    memset(s, 0, sizeof(*s));
}

size_t urf_testpage_next(urf_testpage_t *s, uint8_t *buf, size_t cap)
{
    size_t written = 0;

    while (written < cap && !s->finished) {
        if (s->header_pos < sizeof(urf_header)) {
            size_t n = sizeof(urf_header) - s->header_pos;
            if (n > cap - written) {
                n = cap - written;
            }
            memcpy(buf + written, urf_header + s->header_pos, n);
            written += n;
            s->header_pos += n;
            continue;
        }
        if (s->y >= URF_TESTPAGE_HEIGHT) {
            s->finished = 1;
            continue;
        }
        if (s->group_left == 0 && s->pack_pos == 0 && !s->packet_pending) {
            s->group_left = group_length(s->y);
            make_line(s->line, s->y);
            buf[written++] = (uint8_t)(s->group_left - 1);
            continue;
        }
        if (!s->packet_pending) {
            if (s->pack_pos >= URF_TESTPAGE_WIDTH) {
                s->pack_pos = 0;
                s->y += s->group_left;
                s->group_left = 0;
                continue;
            }
            begin_packet(s);
        }
        while (written < cap && s->packet_pos < s->packet_len) {
            buf[written++] = packet_byte(s, s->packet_pos++);
        }
        if (s->packet_pos == s->packet_len) {
            s->pack_pos = s->packet_source + s->packet_pixels;
            s->packet_pending = 0;
        }
    }
    return written;
}
