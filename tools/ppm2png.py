#!/usr/bin/env python3
"""tools/ppm2png.py — convert P6 PPM to PNG (stdlib only)."""
import struct
import sys
import zlib


def ppm_to_png(src: bytes) -> bytes:
    # parse P6 header (may contain comments)
    if not src.startswith(b"P6"):
        raise ValueError("not a P6 ppm")
    pos = 3
    vals = []
    while len(vals) < 3:
        while pos < len(src) and src[pos : pos + 1].isspace():
            pos += 1
        if src[pos : pos + 1] == b"#":
            pos = src.index(b"\n", pos) + 1
            continue
        start = pos
        while pos < len(src) and not src[pos : pos + 1].isspace():
            pos += 1
        vals.append(int(src[start:pos]))
    pos += 1  # single whitespace after maxval
    w, h, maxval = vals
    data = src[pos : pos + w * h * 3]
    if len(data) < w * h * 3:
        raise ValueError("truncated ppm")

    raw = b"".join(b"\x00" + data[y * w * 3 : (y + 1) * w * 3] for y in range(h))

    def chunk(typ: bytes, payload: bytes) -> bytes:
        return (
            struct.pack(">I", len(payload))
            + typ
            + payload
            + struct.pack(">I", zlib.crc32(typ + payload) & 0xFFFFFFFF)
        )

    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )


if __name__ == "__main__":
    src = open(sys.argv[1], "rb").read()
    open(sys.argv[2], "wb").write(ppm_to_png(src))
    print(f"{sys.argv[1]} -> {sys.argv[2]}")
