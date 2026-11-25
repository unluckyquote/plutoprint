#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 UnluckyQuote
"""Talk to an ESP32 over Improv WiFi serial: probe | provision SSID (password in IMPROV_PW). Needs pyserial."""
import serial, sys, time, os, glob
port = (glob.glob('/dev/cu.wchusbserial*') or glob.glob('/dev/cu.usbserial*'))[0]
s = serial.Serial(port, 115200, timeout=0.2)
def pkt(t, data):
    b = b'IMPROV' + bytes([1, t, len(data)]) + data
    return b + bytes([sum(b) & 0xFF])
def rpc(cmd, payload=b''):
    return pkt(3, bytes([cmd, len(payload)]) + payload)
def read(seconds):
    end = time.time() + seconds; buf = b''
    while time.time() < end: buf += s.read(512)
    return buf
def parse(buf):
    out = []; i = 0
    while True:
        i = buf.find(b'IMPROV', i)
        if i < 0 or i + 9 > len(buf): break
        n = buf[i+8]
        if i + 10 + n > len(buf): break
        t = buf[i+7]; d = buf[i+9:i+9+n]; ok = (sum(buf[i:i+9+n]) & 0xFF) == buf[i+9+n]
        out.append((t, d, ok)); i += 10 + n
    return out
def strings(d):
    cmd, ln = d[0], d[1]; p = d[2:2+ln]; r = []; i = 0
    while i < len(p): r.append(p[i+1:i+1+p[i]].decode(errors='replace')); i += 1 + p[i]
    return cmd, r
def show(buf):
    for t, d, ok in parse(buf):
        if t == 4: print('  result', strings(d), 'checksum ok' if ok else 'BAD CHECKSUM')
        else: print('  type', t, d.hex(), 'ok' if ok else 'BAD')
mode = sys.argv[1]
s.reset_input_buffer()
if mode == 'probe':
    for name, c in (('state', 2), ('info', 3)):
        print(name); s.write(rpc(c)); show(read(1.5))
    print('scan'); s.write(rpc(4)); show(read(8))
elif mode == 'badpw':
    ssid = sys.argv[2].encode(); pw = b'definitely-wrong-pw'
    print('provision with a wrong password'); s.write(rpc(1, bytes([len(ssid)]) + ssid + bytes([len(pw)]) + pw)); show(read(30))
elif mode == 'provision':
    ssid = sys.argv[2].encode(); pw = os.environ['IMPROV_PW'].encode()
    print('provision'); s.write(rpc(1, bytes([len(ssid)]) + ssid + bytes([len(pw)]) + pw)); show(read(30))
