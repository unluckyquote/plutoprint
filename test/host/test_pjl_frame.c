// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pjl_frame.h"

int main(void)
{
    static const uint8_t expected_header[] =
        "\x1b%-12345X@PJL ENTER LANGUAGE=URF\r\n";
    static const uint8_t expected_trailer[] = "\x1b%-12345X";
    uint8_t header[sizeof(expected_header) - 1];
    uint8_t trailer[sizeof(expected_trailer) - 1];

    assert(pjl_frame_header(header, sizeof(header)) == 34);
    assert(memcmp(header, expected_header, sizeof(header)) == 0);
    assert(pjl_frame_trailer(trailer, sizeof(trailer)) == 9);
    assert(memcmp(trailer, expected_trailer, sizeof(trailer)) == 0);

    assert(pjl_frame_header(header, sizeof(header) - 1) == 0);
    assert(pjl_frame_trailer(trailer, sizeof(trailer) - 1) == 0);

    return 0;
}
