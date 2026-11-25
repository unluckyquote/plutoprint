// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#include "raster_convert.h"

#include <stdbool.h>
#include <string.h>

#define PWG_PAGE_HEADER 1796
#define URF_PAGE_HEADER 32
#define OFF_HW_RES 276
#define OFF_WIDTH 372
#define OFF_HEIGHT 376
#define OFF_BITS_PER_COLOR 384
#define OFF_BITS_PER_PIXEL 388
#define OFF_BYTES_PER_LINE 392
#define OFF_COLOR_SPACE 400
#define OFF_QUALITY 484
#define MAX_PAGES 512

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

/* Walks the compressed lines of one page; returns the number of bytes used or 0 if malformed. */
static size_t page_data_size(const uint8_t *data, size_t avail, uint32_t height, uint32_t width, uint32_t px)
{
    size_t at = 0;
    uint32_t rows = 0;
    while (rows < height) {
        if (at >= avail) return 0;
        uint32_t repeat = (uint32_t)data[at++] + 1;
        if (repeat > height - rows) return 0;
        uint32_t done = 0;
        while (done < width) {
            if (at >= avail) return 0;
            uint8_t code = data[at++];
            if (code == 128) {          /* fill the rest of the line with white */
                done = width;
            } else if (code < 128) {    /* one pixel repeated code + 1 times */
                if (at + px > avail || done + code + 1 > width) return 0;
                at += px;
                done += (uint32_t)code + 1;
            } else {                    /* 257 - code literal pixels */
                uint32_t count = 257u - code;
                if (at + (size_t)count * px > avail || done + count > width) return 0;
                at += (size_t)count * px;
                done += count;
            }
        }
        rows += repeat;
    }
    return at;
}

size_t raster_pwg_to_urf(uint8_t *buf, size_t len)
{
    if (buf == NULL || len < 4 + PWG_PAGE_HEADER || memcmp(buf, "RaS2", 4) != 0) return 0;

    /* First pass: check every page and count them. */
    size_t at = 4;
    uint32_t pages = 0;
    while (at < len) {
        if (len - at < PWG_PAGE_HEADER || ++pages > MAX_PAGES) return 0;
        const uint8_t *h = buf + at;
        uint32_t width = be32(h + OFF_WIDTH), height = be32(h + OFF_HEIGHT);
        uint32_t cs = be32(h + OFF_COLOR_SPACE);
        uint32_t bpp = be32(h + OFF_BITS_PER_PIXEL);
        bool gray = bpp == 8 && (cs == 0 || cs == 18);
        bool rgb = bpp == 24 && (cs == 1 || cs == 19);
        if (width == 0 || height == 0 || be32(h + OFF_BITS_PER_COLOR) != 8 || (!gray && !rgb) ||
            be32(h + OFF_BYTES_PER_LINE) != width * (bpp / 8)) return 0;
        size_t used = page_data_size(h + PWG_PAGE_HEADER, len - at - PWG_PAGE_HEADER, height, width, bpp / 8);
        if (used == 0) return 0;
        at += PWG_PAGE_HEADER + used;
    }
    if (pages == 0) return 0;

    /* Second pass: rewrite in place. The output never runs ahead of the input because a URF page
     * header is much smaller than a PWG one. */
    size_t read = 4, write = 0;
    memcpy(buf, "UNIRAST", 8);
    put_be32(buf + 8, pages);
    write = 12;
    for (uint32_t page = 0; page < pages; page++) {
        uint8_t header[URF_PAGE_HEADER] = { 0 };
        const uint8_t *h = buf + read;
        uint32_t width = be32(h + OFF_WIDTH), height = be32(h + OFF_HEIGHT);
        uint32_t quality = be32(h + OFF_QUALITY);
        uint32_t bpp = be32(h + OFF_BITS_PER_PIXEL);
        header[0] = (uint8_t)bpp;                       /* bits per pixel */
        header[1] = bpp == 24 ? 1 : 0;                  /* sRGB (assumed value 1) or sGray */
        header[2] = 1;                                  /* one-sided */
        header[3] = (quality >= 3 && quality <= 5) ? (uint8_t)quality : 4;
        put_be32(header + 12, width);
        put_be32(header + 16, height);
        put_be32(header + 20, be32(h + OFF_HW_RES));
        size_t used = page_data_size(h + PWG_PAGE_HEADER, len - read - PWG_PAGE_HEADER, height, width, bpp / 8);
        memmove(buf + write + URF_PAGE_HEADER, buf + read + PWG_PAGE_HEADER, used);
        memcpy(buf + write, header, URF_PAGE_HEADER);
        write += URF_PAGE_HEADER + used;
        read += PWG_PAGE_HEADER + used;
    }
    return write;
}
