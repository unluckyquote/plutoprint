// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#include "pjl_frame.h"

#include <string.h>

static const uint8_t header[] = "\x1b%-12345X@PJL ENTER LANGUAGE=URF\r\n";
static const uint8_t trailer[] = "\x1b%-12345X";

size_t pjl_frame_header(uint8_t *buf, size_t cap)
{
    const size_t len = sizeof(header) - 1;

    if (cap < len) {
        return 0;
    }

    memcpy(buf, header, len);
    return len;
}

size_t pjl_frame_trailer(uint8_t *buf, size_t cap)
{
    const size_t len = sizeof(trailer) - 1;

    if (cap < len) {
        return 0;
    }

    memcpy(buf, trailer, len);
    return len;
}
