#!/usr/bin/env python3
"""Makes the preview's stand-in for the GameCube's ROM font: characters 32..255
of a TrueType font in 24x24 cells on one 8-bit sheet, plus their widths.
The console draws its own IPL font; this only has to look close enough.

usage: font_atlas.py FONT.ttf OUT.bin
"""
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

CELL, FIRST, LAST, COLS = 24, 32, 255, 16


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    font = ImageFont.truetype(sys.argv[1], 19, layout_engine=ImageFont.Layout.BASIC)
    rows = (LAST - FIRST + COLS) // COLS
    sheet = Image.new("L", (COLS * CELL, rows * CELL), 0)
    draw = ImageDraw.Draw(sheet)
    widths = [0] * 256
    ascent = font.getmetrics()[0]
    for c in range(FIRST, LAST + 1):
        ch = bytes([c]).decode("latin-1")
        i = c - FIRST
        x, y = (i % COLS) * CELL, (i // COLS) * CELL
        adv = int(round(font.getlength(ch))) + 1
        widths[c] = max(4, min(CELL, adv))
        draw.text((x + 1, y + 20 - ascent), ch, font=font, fill=255)
    with open(sys.argv[2], "wb") as f:
        f.write(struct.pack("<7i", CELL, CELL, FIRST, LAST, COLS, sheet.width, sheet.height))
        f.write(bytes(widths))
        f.write(sheet.tobytes())


if __name__ == "__main__":
    main()
