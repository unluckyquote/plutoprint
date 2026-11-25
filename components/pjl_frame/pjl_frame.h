// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#ifndef PJL_FRAME_H
#define PJL_FRAME_H

#include <stddef.h>
#include <stdint.h>

size_t pjl_frame_header(uint8_t *buf, size_t cap);
size_t pjl_frame_trailer(uint8_t *buf, size_t cap);

#endif
