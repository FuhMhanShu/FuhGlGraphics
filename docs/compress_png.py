#!/usr/bin/env python3
"""Re-compress the checked-in doc screenshots losslessly (same pixels, same size).

The C++ screenshot writer (examples/screenshot/png_writer.hpp) emits "stored"
(uncompressed) DEFLATE blocks to stay dependency-free, so a 1280x720 reference PNG is
~3.7 MB. This script re-encodes PNGs with per-row filtering (None/Sub/Up, pick the
smallest) + zlib level 9 — typically ~8x smaller with byte-identical pixels, so the CI
perceptual diff (docs/screenshot_diff.py) is unaffected.

Pure Python 3 (zlib + struct, no PIL/numpy):
    python3 docs/compress_png.py docs/images          # whole directory
    python3 docs/compress_png.py docs/images/g17_bloom_on.png docs/images/g18_picking_marker.png

Exit code 0 on success, 1 on any decode/encode error.
"""

import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from screenshot_diff import read_png_rgba  # noqa: E402


def _chunk(ctype, data):
    return struct.pack(">I", len(data)) + ctype + data + struct.pack(">I", zlib.crc32(ctype + data) & 0xFFFFFFFF)


def write_png_rgba(path, w, h, rgba, level=9):
    """Encodes 8-bit RGBA with per-row best-of-{None,Sub,Up} filtering + zlib level."""
    stride = w * 4
    raw = bytearray()
    prev = bytearray(stride)
    for y in range(h):
        row = rgba[y * stride : (y + 1) * stride]
        best = None
        best_cost = None
        for ftype in (0, 1, 2):
            f = bytearray(stride)
            cost = 0
            if ftype == 0:
                f[:] = row
            elif ftype == 1:  # Sub
                for i in range(stride):
                    left = row[i - 4] if i >= 4 else 0
                    v = (row[i] - left) & 255
                    f[i] = v
                    cost += v if v < 128 else 256 - v
            else:  # Up
                for i in range(stride):
                    v = (row[i] - prev[i]) & 255
                    f[i] = v
                    cost += v if v < 128 else 256 - v
            if best is None or cost < best_cost:
                best = f
                best_cost = cost
                best_type = ftype
        raw.append(best_type)
        raw += best
        prev = row
    comp = zlib.compress(bytes(raw), level)
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)  # 8-bit RGBA
    png = b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", ihdr) + _chunk(b"IDAT", comp) + _chunk(b"IEND", b"")
    with open(path, "wb") as fh:
        fh.write(png)
    return len(png)


def compress_file(path):
    before = os.path.getsize(path)
    w, h, px = read_png_rgba(path)
    after = write_png_rgba(path, w, h, px)
    # verify: decode again and compare pixels byte-for-byte
    w2, h2, px2 = read_png_rgba(path)
    assert (w2, h2) == (w, h) and bytes(px2) == bytes(px), f"{path}: pixel mismatch after re-encode"
    return before, after, w, h


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    paths = []
    for a in argv[1:]:
        if os.path.isdir(a):
            paths += sorted(os.path.join(a, f) for f in os.listdir(a) if f.endswith(".png"))
        else:
            paths.append(a)
    total_before = total_after = 0
    for p in paths:
        before, after, w, h = compress_file(p)
        total_before += before
        total_after += after
        print(f"{p}: {w}x{h}  {before/1e6:.2f} MB -> {after/1e6:.2f} MB  ({100.0*after/max(before,1):.1f}%)")
    print(f"TOTAL: {total_before/1e6:.1f} MB -> {total_after/1e6:.1f} MB "
          f"({100.0*total_after/max(total_before,1):.1f}%) over {len(paths)} files")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
