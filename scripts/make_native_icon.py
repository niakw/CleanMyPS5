#!/usr/bin/env python3
"""Build an original 512x512 RGBA PNG title icon with only stdlib.

No private images, fonts, downloaded assets, or third-party branding.
"""
from __future__ import annotations

import math
import struct
import sys
import zlib
from pathlib import Path

SIZE = 512

def chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

def pixel(x: int, y: int) -> tuple[int,int,int,int]:
    dist = math.hypot(x - 256, y - 256)
    glow = max(0.0, 1.0 - dist / 420.0)
    r, g, b = 12 + int(8 * glow), 22 + int(24 * glow), 39 + int(38 * glow)
    # Stacked storage bars framing a centered C-shaped gauge.
    if 96 <= x <= 416 and 91 <= y <= 111:
        r,g,b = 41,199,234
    if 96 <= x <= 416 and 401 <= y <= 421:
        r,g,b = 41,199,234
    ring = math.hypot(x-256,y-260)
    angle = math.atan2(y-260,x-256)
    if 135 <= ring <= 164 and not (-0.78 <= angle <= 0.78):
        r,g,b = 54,213,244
    if 112 <= ring <= 126 and not (-0.52 <= angle <= 0.52):
        r,g,b = 24,107,168
    if 292 <= x <= 365 and 251 <= y <= 268:
        r,g,b = 238,249,255
    return r,g,b,255

def make(path: Path) -> None:
    image = bytearray()
    for y in range(SIZE):
        image.append(0)
        for x in range(SIZE):
            image.extend(pixel(x,y))
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(image), 9))
    png += chunk(b"IEND", b"")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)
    assert len(png) > 1000

if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: make_native_icon.py OUT.png")
    make(Path(sys.argv[1]))
