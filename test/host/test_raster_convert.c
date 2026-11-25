// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raster_convert.h"

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

/* Builds a PWG raster document out of the URF fixture: same compressed lines, PWG headers. */
static uint8_t *urf_to_pwg(const uint8_t *urf, size_t len, size_t *out_len)
{
    uint8_t *out = calloc(len + 4 + 1796, 1);
    memcpy(out, "RaS2", 4);
    uint8_t *h = out + 4;
    uint32_t width = (urf[24] << 24) | (urf[25] << 16) | (urf[26] << 8) | urf[27];
    uint32_t height = (urf[28] << 24) | (urf[29] << 16) | (urf[30] << 8) | urf[31];
    uint32_t res = (urf[32] << 24) | (urf[33] << 16) | (urf[34] << 8) | urf[35];
    put_be32(h + 276, res); put_be32(h + 280, res);
    put_be32(h + 372, width); put_be32(h + 376, height);
    put_be32(h + 384, 8); put_be32(h + 388, 8); put_be32(h + 392, width);
    put_be32(h + 400, 18); put_be32(h + 484, 4);
    memcpy(h + 1796, urf + 44, len - 44);
    *out_len = 4 + 1796 + (len - 44);
    return out;
}

int main(void)
{
    FILE *f = fopen("../fixtures/hello0.urf", "rb");   /* tests run from test/host */
    assert(f != NULL);
    uint8_t *urf = malloc(4 << 20);
    size_t urf_len = fread(urf, 1, 4 << 20, f);
    fclose(f);

    size_t pwg_len;
    uint8_t *pwg = urf_to_pwg(urf, urf_len, &pwg_len);
    size_t converted = raster_pwg_to_urf(pwg, pwg_len);
    assert(converted == urf_len);
    assert(memcmp(pwg, urf, urf_len) == 0);

    /* Truncated, wrong colour depth, not PWG, empty. */
    uint8_t *cut = urf_to_pwg(urf, urf_len, &pwg_len);
    assert(raster_pwg_to_urf(cut, pwg_len - 10) == 0);
    cut = urf_to_pwg(urf, urf_len, &pwg_len);
    put_be32(cut + 4 + 388, 24);
    assert(raster_pwg_to_urf(cut, pwg_len) == 0);
    assert(raster_pwg_to_urf((uint8_t *)"UNIRAST", 8) == 0);
    assert(raster_pwg_to_urf(NULL, 0) == 0);

    /* One 24-bit colour page: width 4, height 2, a red run on each line. */
    uint8_t colour[4 + 1796 + 10];
    memset(colour, 0, sizeof(colour));
    memcpy(colour, "RaS2", 4);
    uint8_t *h = colour + 4;
    put_be32(h + 276, 600); put_be32(h + 372, 4); put_be32(h + 376, 2);
    put_be32(h + 384, 8); put_be32(h + 388, 24); put_be32(h + 392, 12); put_be32(h + 400, 19);
    const uint8_t rows[10] = { 0x00, 0x03, 0xff, 0x00, 0x00, 0x00, 0x03, 0xff, 0x00, 0x00 };
    memcpy(h + 1796, rows, sizeof(rows));
    size_t out = raster_pwg_to_urf(colour, sizeof(colour));
    assert(out == 12 + 32 + 10);
    assert(colour[12] == 24 && colour[13] == 1);
    assert(memcmp(colour + 44, rows, sizeof(rows)) == 0);
    return 0;
}
