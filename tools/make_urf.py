#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 UnluckyQuote
"""Write an Apple URF (UNIRAST) 8-bit gray A4 300dpi test page.

usage: make_urf.py OUT.urf [colorspace=0]
       make_urf.py --verify FILE
"""
import struct
import sys

W, H, DPI = 2480, 3508, 300


def pixel_line(y):
    row = bytearray([255]) * W
    inset, t = 100, 3
    in_frame_y = inset <= y < H - inset
    if in_frame_y and (y < inset + t or y >= H - inset - t):
        for x in range(inset, W - inset):
            row[x] = 0
    elif in_frame_y:
        for x in list(range(inset, inset + t)) + list(range(W - inset - t, W - inset)):
            row[x] = 0
    if (H - 600) // 2 <= y < (H + 600) // 2:
        for x in range((W - 600) // 2, (W + 600) // 2):
            row[x] = 0
    return bytes(row)


def pack_line(row):
    out = bytearray()
    i = 0
    n = len(row)
    while i < n:
        j = i
        while j + 1 < n and row[j + 1] == row[i] and j - i < 127:
            j += 1
        run = j - i + 1
        if run >= 2:
            out += bytes([run - 1, row[i]])
            i = j + 1
        else:
            k = i
            while k < n and k - i < 128 and (k + 1 >= n or row[k + 1] != row[k]):
                k += 1
            lit = row[i:k]
            if not lit:
                lit = row[i:i + 1]
                k = i + 1
            out += bytes([257 - len(lit)]) + lit
            i = k
    return bytes(out)


def write(path, cs):
    lines = [pixel_line(y) for y in range(H)]
    with open(path, "wb") as f:
        f.write(b"UNIRAST\0" + struct.pack(">I", 1))
        f.write(bytes([8, cs, 1, 4]) + bytes(8) + struct.pack(">III", W, H, DPI) + bytes(8))
        y = 0
        while y < H:
            c = 1
            while y + c < H and lines[y + c] == lines[y] and c < 256:
                c += 1
            f.write(bytes([c - 1]) + pack_line(lines[y]))
            y += c


def verify(path):
    d = open(path, "rb").read()
    assert d[:8] == b"UNIRAST\0", "bad magic"
    assert struct.unpack(">I", d[8:12])[0] == 1
    ph = d[12:44]
    assert struct.unpack(">III", ph[12:24]) == (W, H, DPI)
    p, total = 44, 0
    while p < len(d):
        reps = d[p] + 1
        p += 1
        px = 0
        while px < W:
            c = d[p]
            p += 1
            if c < 128:
                px += c + 1
                p += 1
            elif c == 128:
                px = W
            else:
                n = 257 - c
                px += n
                p += n
        assert px == W, f"line width {px}"
        total += reps
    assert total == H, f"line count {total}"
    print("OK", len(d), "bytes")


if __name__ == "__main__":
    if sys.argv[1] == "--verify":
        verify(sys.argv[2])
    else:
        write(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 0)
