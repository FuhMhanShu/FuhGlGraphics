#!/usr/bin/env python3
"""Perceptual screenshot diff for the CI docs-screenshots job (G10).

Compares every PNG rendered into the CI output dir (e.g. docs/images/ci/) against the
checked-in reference in docs/images/ and fails when the PSNR drops below a threshold
(llvmpipe renders are deterministic, so identical inputs should give ~infinite PSNR;
the threshold only tolerates minor driver differences).

Pure Python 3 (zlib + struct, no PIL/numpy):
    python3 docs/screenshot_diff.py docs/images docs/images/ci --min-psnr 30

Exit code 0 when all pairs pass (or no pairs found), 1 otherwise.
"""

import math
import os
import struct
import sys
import zlib


def read_png_rgba(path):
    """Decodes a non-interlaced 8-bit RGBA/RGB PNG into (w, h, bytes RGBA, top-down rows)."""
    with open(path, "rb") as fh:
        data = fh.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos = 8
    width = height = None
    bit_depth = color_type = None
    interlace = 0
    idat = bytearray()
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos : pos + 4])
        ctype = data[pos + 4 : pos + 8]
        chunk = data[pos + 8 : pos + 8 + length]
        if ctype == b"IHDR":
            width, height, bit_depth, color_type, _comp, _filt, interlace = struct.unpack(">IIBBBBB", chunk)
        elif ctype == b"IDAT":
            idat += chunk
        elif ctype == b"IEND":
            break
        pos += 12 + length
    if width is None or bit_depth != 8 or interlace != 0:
        raise ValueError(f"{path}: unsupported PNG (bit_depth={bit_depth}, interlace={interlace})")
    channels = {0: 1, 2: 3, 4: 2, 6: 4}[color_type]
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    out = bytearray(width * height * 4)
    prev = bytearray(stride)
    p = 0
    for y in range(height):
        f = raw[p]
        p += 1
        line = bytearray(raw[p : p + stride])
        p += stride
        if f == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 255
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + (a + prev[i]) // 2) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = prev[i]
                c = prev[i - channels] if i >= channels else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        for x in range(width):
            so = x * channels
            do = (y * width + x) * 4
            if channels == 4:
                out[do : do + 4] = line[so : so + 4]
            elif channels == 3:
                out[do : do + 3] = line[so : so + 3]
                out[do + 3] = 255
            elif channels == 2:
                out[do] = out[do + 1] = out[do + 2] = line[so]
                out[do + 3] = line[so + 1]
            else:
                out[do] = out[do + 1] = out[do + 2] = line[so]
                out[do + 3] = 255
        prev = line
    return width, height, out


def compare(ref_path, ci_path):
    """Returns (psnr_db, mean_abs_error) for two same-size RGBA images."""
    w1, h1, a = read_png_rgba(ref_path)
    w2, h2, b = read_png_rgba(ci_path)
    if (w1, h1) != (w2, h2):
        raise ValueError(f"size mismatch: {ref_path} is {w1}x{h1}, {ci_path} is {w2}x{h2}")
    mse = 0.0
    total_abs = 0
    n = len(a)
    for i in range(0, n, 4):
        for c in range(3):
            d = a[i + c] - b[i + c]
            mse += d * d
            total_abs += abs(d)
    mse /= (w1 * h1 * 3)
    psnr = 99.0 if mse == 0 else 10.0 * math.log10(255.0 * 255.0 / mse)
    return psnr, total_abs / (w1 * h1 * 3)


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    ref_dir, ci_dir = argv[1], argv[2]
    min_psnr = 30.0
    if "--min-psnr" in argv:
        min_psnr = float(argv[argv.index("--min-psnr") + 1])
    refs = {f for f in os.listdir(ref_dir) if f.endswith(".png")}
    cis = {f for f in os.listdir(ci_dir) if f.endswith(".png")}
    pairs = sorted(refs & cis)
    if not pairs:
        print(f"screenshot_diff: no PNG pairs between {ref_dir} and {ci_dir}")
        return 0
    failed = 0
    for name in pairs:
        ref_path = os.path.join(ref_dir, name)
        ci_path = os.path.join(ci_dir, name)
        try:
            psnr, mae = compare(ref_path, ci_path)
        except ValueError as exc:
            print(f"FAIL {name}: {exc}")
            failed += 1
            continue
        status = "ok" if psnr >= min_psnr else "FAIL"
        if psnr < min_psnr:
            failed += 1
        print(f"{status} {name}: PSNR={psnr:.2f} dB (min {min_psnr:.1f}), meanAbsErr={mae:.3f}")
    missing = sorted(refs - cis)
    for name in missing:
        print(f"note: no CI render for reference {name}")
    print(f"screenshot_diff: {len(pairs) - failed}/{len(pairs)} pairs within PSNR >= {min_psnr:.1f} dB")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
