// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#pragma once

#include <stddef.h>
#include <stdint.h>

#define URF_TESTPAGE_WIDTH 2480
#define URF_TESTPAGE_HEIGHT 3508

typedef struct {
    uint8_t line[URF_TESTPAGE_WIDTH];
    size_t header_pos;
    size_t y;
    size_t group_left;
    size_t pack_pos;
    size_t packet_pos;
    size_t packet_len;
    size_t packet_pixels;
    size_t packet_source;
    uint8_t packet_value;
    uint8_t packet_literal;
    uint8_t packet_pending;
    uint8_t finished;
} urf_testpage_t;

void urf_testpage_init(urf_testpage_t *s);
size_t urf_testpage_next(urf_testpage_t *s, uint8_t *buf, size_t cap);
