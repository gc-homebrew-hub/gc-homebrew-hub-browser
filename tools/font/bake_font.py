#!/usr/bin/env python3
"""Bakes the program's text sizes from Inter into source/font_data.c: for each
size, printable ASCII rendered at that exact pixel size (FreeType, hinted) and
packed into one GX texture in IA4 format (4-bit intensity, 4-bit alpha, in
8x4 tiles), plus each glyph's place in it and its metrics. The program draws
these 1:1 at whole-pixel positions, so text is never scaled.

"make font" runs it; the output is committed, so building needs no Python.
usage: bake_font.py OUT.c
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
# (enum name in font.h, font file, size in pixels); the order is font.h's
FACES = [
    ("FS_KEY",   "Inter-SemiBold.ttf", 13),
    ("FS_SMALL", "Inter-Medium.ttf",   15),
    ("FS_BODY",  "Inter-Medium.ttf",   18),
    ("FS_BIG",   "Inter-SemiBold.ttf", 22),
]
FIRST, LAST = 32, 126
TEX_W = 256


def bake(path, px):
    font = ImageFont.truetype(os.path.join(HERE, path), px, layout_engine=ImageFont.Layout.BASIC)
    asc, desc = font.getmetrics()
    glyphs = []
    for c in range(FIRST, LAST + 1):
        ch = chr(c)
        pad = px
        img = Image.new("L", (px * 3, asc + desc + 2 * pad), 0)
        ImageDraw.Draw(img).text((pad, pad + asc), ch, font=font, fill=255, anchor="ls")
        box = img.getbbox()
        adv = int(round(font.getlength(ch)))
        if box is None:
            glyphs.append((None, 0, 0, adv))
        else:
            glyphs.append((img.crop(box), box[0] - pad, box[1] - pad, adv))
    # pack in shelves, a pixel of space around each glyph
    x = y = shelf = 0
    places = []
    for g in glyphs:
        im = g[0]
        if im is None:
            places.append((0, 0))
            continue
        if x + im.width + 1 > TEX_W:
            x, y, shelf = 0, y + shelf + 1, 0
        places.append((x, y))
        x += im.width + 1
        shelf = max(shelf, im.height)
    tex_h = (y + shelf + 3) // 4 * 4
    sheet = Image.new("L", (TEX_W, tex_h), 0)
    for g, (gx, gy) in zip(glyphs, places):
        if g[0] is not None:
            sheet.paste(g[0], (gx, gy))
    return asc + desc, asc, glyphs, places, sheet


def ia4_tiles(sheet):
    """The sheet as GX IA4: 8x4 tiles in rows, each texel alpha << 4 | intensity."""
    px = sheet.load()
    out = bytearray()
    for ty in range(0, sheet.height, 4):
        for tx in range(0, sheet.width, 8):
            for y in range(ty, ty + 4):
                for x in range(tx, tx + 8):
                    a = (px[x, y] * 15 + 127) // 255
                    out.append(a << 4 | 15)
    return out


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    lines = [
        "/* font_data.c - written by tools/font/bake_font.py (\"make font\"); do not edit.",
        " * Inter, Copyright (c) 2016 The Inter Project Authors, SIL Open Font",
        " * License 1.1 (tools/font/OFL.txt). */",
        '#include "font_data.h"',
        "",
    ]
    faces = []
    for name, path, px in FACES:
        line_h, asc, glyphs, places, sheet = bake(path, px)
        sym = name.lower()[3:]
        data = ia4_tiles(sheet)
        lines.append(f"/* {path} at {px} px: {sheet.width}x{sheet.height}, {len(data)} bytes */")
        lines.append(f"static const u8 tex_{sym}[{len(data)}] __attribute__((aligned(32))) = {{")
        for i in range(0, len(data), 24):
            lines.append("    " + ",".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",")
        lines.append("};")
        lines.append(f"static const FontGlyph glyphs_{sym}[{LAST - FIRST + 1}] = {{")
        for c, (g, (gx, gy)) in zip(range(FIRST, LAST + 1), zip(glyphs, places)):
            w, h = (g[0].width, g[0].height) if g[0] is not None else (0, 0)
            ch = "space" if c == 32 else "'\\\\'" if c == 92 else f"'{chr(c)}'"
            lines.append(f"    {{ {gx}, {gy}, {w}, {h}, {g[1]}, {g[2]}, {g[3]} }},  /* {ch} */")
        lines.append("};")
        lines.append("")
        faces.append(f"    [{name}] = {{ {sheet.width}, {sheet.height}, {line_h}, {asc}, glyphs_{sym}, tex_{sym} }},")
    lines.append("const FontFace font_faces[FS_COUNT] = {")
    lines += faces
    lines.append("};")
    with open(sys.argv[1], "w") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
