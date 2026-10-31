/* font_data.h - the baked text sizes in font_data.c (tools/font/bake_font.py). */
#pragma once
#include "font.h"

#define FONT_FIRST 32                    /* printable ASCII */
#define FONT_LAST  126

/* A glyph's box in its texture, where it sits relative to the pen (left of
 * the pen, below the top of the line) and how far the pen then moves */
typedef struct { u16 x, y; u8 w, h; s8 left, top; u8 advance; } FontGlyph;

/* One size: an IA4 texture of tex_w x tex_h, the line's height and baseline */
typedef struct {
    u16 tex_w, tex_h;
    u8 line_h, baseline;
    const FontGlyph *glyphs;             /* FONT_FIRST..FONT_LAST */
    const u8 *tex;
} FontFace;

extern const FontFace font_faces[FS_COUNT];
