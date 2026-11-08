#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""
Make pie.c, the picture pieboot shows while it boots without -v: the pie
on black, and a question mark over it for when there is no kernel to boot.

usage: mkpie.py pie.png > pie.c

pie.png is 128x128 RGBA. The pie is cropped to its pixels. The question
mark is drawn here, 64x64, and laid over the middle of the pie. Both are
blended over black, share one palette of at most 256 colours (median cut)
and are run-length coded as { count, palette index } byte pairs, row after
row, as fbcon.c draws them.
"""
import struct
import sys
import zlib

SIZE = 128


def read_png(path):
    """An 8-bit RGBA, non-interlaced PNG -> rows of (r, g, b, a)."""
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit("mkpie: %s is not a PNG" % path)
    pos, idat = 8, b""
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, colour, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or colour != 6 or interlace:
                raise SystemExit("mkpie: %s is not 8-bit RGBA without interlacing" % path)
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 4
    rows, prev = [], bytearray(stride)
    for y in range(height):
        kind = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            left = line[i - 4] if i >= 4 else 0
            up = prev[i]
            corner = prev[i - 4] if i >= 4 else 0
            if kind == 1:
                line[i] = (line[i] + left) & 0xff
            elif kind == 2:
                line[i] = (line[i] + up) & 0xff
            elif kind == 3:
                line[i] = (line[i] + (left + up) // 2) & 0xff
            elif kind == 4:
                p = left + up - corner
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - corner)
                line[i] = (line[i] + (left if pa <= pb and pa <= pc else up if pb <= pc else corner)) & 0xff
        rows.append([tuple(line[x * 4:x * 4 + 4]) for x in range(width)])
        prev = line
    return rows


def over(top, bottom):
    """RGBA top over opaque RGB bottom."""
    r, g, b, a = top
    return tuple((c * a + d * (255 - a) + 127) // 255 for c, d in zip((r, g, b), bottom))


QUESTION_RGB = (229, 57, 53)


def question():
    """A red question mark, 64x64 RGBA, 4x4 supersampled: an arc open to the
    lower left, a stem below its lower right, and a dot."""
    def inside(x, y):
        d2 = (x - 32) ** 2 + (y - 20) ** 2
        if 7 ** 2 <= d2 <= 14 ** 2 and not (x < 32 and y > 20):
            return True
        if 28.5 <= x <= 35.5 and 27 <= y <= 40:
            return True
        return (x - 32) ** 2 + (y - 48) ** 2 <= 4.5 ** 2
    out = []
    for y in range(64):
        row = []
        for x in range(64):
            n = sum(inside(x + (i + 0.5) / 4, y + (j + 0.5) / 4) for i in range(4) for j in range(4))
            row.append(QUESTION_RGB + ((255 * n + 8) // 16,))
        out.append(row)
    return out


def bbox(img, keep):
    """(x, y, w, h) of the pixels for which keep(pixel)."""
    ys = [y for y, row in enumerate(img) if any(keep(p) for p in row)]
    xs = [x for x in range(len(img[0])) if any(keep(row[x]) for row in img)]
    return xs[0], ys[0], xs[-1] + 1 - xs[0], ys[-1] + 1 - ys[0]


def median_cut(colours, n):
    """At most n colours for a {colour: count} histogram."""
    boxes = [list(colours.items())]
    while len(boxes) < n:
        def spread(box):
            return max(max(c[i] for c, _ in box) - min(c[i] for c, _ in box) for i in range(3))
        box = max((b for b in boxes if len(b) > 1), key=lambda b: spread(b) * sum(k for _, k in b), default=None)
        if box is None:
            break
        axis = max(range(3), key=lambda i: max(c[i] for c, _ in box) - min(c[i] for c, _ in box))
        box.sort(key=lambda e: e[0][axis])
        total, half = sum(k for _, k in box), 0
        for cut, (_, k) in enumerate(box):
            half += k
            if half * 2 >= total:
                break
        cut = max(1, min(cut + 1, len(box) - 1))
        boxes.remove(box)
        boxes += [box[:cut], box[cut:]]
    palette = []
    for box in boxes:
        total = sum(k for _, k in box)
        palette.append(tuple((sum(c[i] * k for c, k in box) + total // 2) // total for i in range(3)))
    return palette


def rle(pixels, index):
    out, run, last = [], 0, None
    for p in pixels:
        i = index[p]
        if i == last and run < 255:
            run += 1
            continue
        if run:
            out += [run, last]
        run, last = 1, i
    out += [run, last]
    return out


def c_bytes(data):
    lines = []
    for i in range(0, len(data), 16):
        lines.append("\t" + " ".join("0x%02x," % b for b in data[i:i + 16]))
    return "\n".join(lines)


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: mkpie.py pie.png > pie.c")
    pie = read_png(sys.argv[1])
    if len(pie) != SIZE or len(pie[0]) != SIZE:
        raise SystemExit("mkpie: pie.png must be %dx%d" % (SIZE, SIZE))

    black = (0, 0, 0)
    canvas = [[over(p, black) for p in row] for row in pie]
    px, py, pw, ph = bbox(pie, lambda p: p[3] > 0)
    small = question()
    qx, qy, qw, qh = bbox(small, lambda p: p[3] > 0)
    ox = px + (pw - qw) // 2 - qx
    oy = py + (ph - qh) // 2 - qy
    with_question = [row[:] for row in canvas]
    for y in range(len(small)):
        for x in range(len(small)):
            if small[y][x][3] and 0 <= y + oy < SIZE and 0 <= x + ox < SIZE:
                with_question[y + oy][x + ox] = over(small[y][x], with_question[y + oy][x + ox])
    # The question mark's box, within the pie's.
    bx, by = max(qx + ox, px), max(qy + oy, py)
    bw, bh = min(qx + ox + qw, px + pw) - bx, min(qy + oy + qh, py + ph) - by

    plain = [canvas[y][x] for y in range(py, py + ph) for x in range(px, px + pw)]
    marked = [with_question[y][x] for y in range(by, by + bh) for x in range(bx, bx + bw)]
    histogram = {}
    for p in plain + marked:
        histogram[p] = histogram.get(p, 0) + 1
    if len(histogram) <= 256:
        palette = sorted(histogram)
        index = {c: i for i, c in enumerate(palette)}
    else:
        palette = median_cut(histogram, 256)
        index = {}
        for c in histogram:
            index[c] = min(range(len(palette)), key=lambda i: sum((a - b) ** 2 for a, b in zip(c, palette[i])))

    plain_rle, marked_rle = rle(plain, index), rle(marked, index)
    print("""/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * The picture pieboot shows while it boots without -v (fbcon.c), made by
 * mkpie.py from pie.png: the pie, and a question mark over it for when
 * there is no kernel to boot. Runs of { count, palette index }, row by row.
 */

#include <stdint.h>
""")
    print("/* 0xRRGGBB */")
    print("const uint32_t pie_palette[%d] = {" % len(palette))
    for i in range(0, len(palette), 8):
        print("\t" + " ".join("0x%06x," % (r << 16 | g << 8 | b) for r, g, b in palette[i:i + 8]))
    print("};\n")
    print("/* The pie: { x, y, width, height } of its box on the 128x128 picture. */")
    print("const uint16_t pie_plain_box[4] = { %d, %d, %d, %d };" % (px, py, pw, ph))
    print("const uint8_t pie_plain[%d] = {\n%s\n};\n" % (len(plain_rle), c_bytes(plain_rle)))
    print("/* The question mark over the pie: the box of the pie it covers, likewise. */")
    print("const uint16_t pie_question_box[4] = { %d, %d, %d, %d };" % (bx, by, bw, bh))
    print("const uint8_t pie_question[%d] = {\n%s\n};" % (len(marked_rle), c_bytes(marked_rle)))
    print("mkpie: %d colours, pie %dx%d in %d bytes, question %dx%d in %d bytes" %
          (len(histogram), pw, ph, len(plain_rle), bw, bh, len(marked_rle)), file=sys.stderr)


if __name__ == "__main__":
    main()
