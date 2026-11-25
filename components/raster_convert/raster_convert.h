// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#pragma once

#include <stddef.h>
#include <stdint.h>

/* Converts a PWG raster document ("RaS2", 8-bit gray or 24-bit sRGB pages) into Apple raster (URF), which the
 * printer decodes natively. Both formats compress the page lines the same way, so only the headers
 * change. Works in place and returns the new length, or 0 if the document is not valid 8-bit gray or 24-bit sRGB PWG raster. */
size_t raster_pwg_to_urf(uint8_t *buf, size_t len);
