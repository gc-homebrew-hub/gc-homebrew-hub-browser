/* font.c - text from bitmaps of Inter baked at each size the program uses
 * (font_data.c). Each glyph is drawn 1:1 from its texture at a whole-pixel
 * position, with no filtering, so the text stays as sharp as it was baked. */
#include "font.h"
#include "font_data.h"
#include <math.h>

static GXTexObj tex[FS_COUNT];

void font_init(void) {
    for (int i = 0; i < FS_COUNT; i++) {
        const FontFace *f = &font_faces[i];
        DCFlushRange((void *)f->tex, f->tex_w * f->tex_h);
        GX_InitTexObj(&tex[i], (void *)f->tex, f->tex_w, f->tex_h, GX_TF_IA4, GX_CLAMP, GX_CLAMP, GX_FALSE);
        GX_InitTexObjFilterMode(&tex[i], GX_NEAR, GX_NEAR);
    }
}

static const FontGlyph *glyph(const FontFace *f, int c) {
    if (c < FONT_FIRST || c > FONT_LAST) c = '?';
    return &f->glyphs[c - FONT_FIRST];
}

float font_height(FontSize s) { return font_faces[s].line_h; }

float font_line_pitch(FontSize s) { return font_faces[s].line_h + 2; }

float font_width_n(const char *t, int n, FontSize s) {
    float w = 0;
    for (int i = 0; i < n && t[i]; i++) w += glyph(&font_faces[s], (unsigned char)t[i])->advance;
    return w;
}

float font_width(const char *t, FontSize s) { return font_width_n(t, 0x7fffffff, s); }

static void draw_n(float x, float y, FontSize s, Color c, const char *t, int n) {
    const FontFace *f = &font_faces[s];
    float px = floorf(x + 0.5f), py = floorf(y + 0.5f);
    for (int i = 0; i < n && t[i]; i++) {
        const FontGlyph *g = glyph(f, (unsigned char)t[i]);
        if (g->w)
            gfx_tex_quad(&tex[s], f->tex_w, f->tex_h, px + g->left, py + g->top, g->w, g->h,
                         g->x, g->y, g->x + g->w, g->y + g->h, c);
        px += g->advance;
    }
}

void font_draw(float x, float y, FontSize s, Color c, const char *t) { draw_n(x, y, s, c, t, 0x7fffffff); }

void font_draw_center(float cx, float y, FontSize s, Color c, const char *t) {
    font_draw(cx - font_width(t, s) / 2, y, s, c, t);
}

void font_draw_right(float rx, float y, FontSize s, Color c, const char *t) {
    font_draw(rx - font_width(t, s), y, s, c, t);
}

void font_draw_clip(float x, float y, FontSize s, Color c, const char *t, float maxw) {
    if (font_width(t, s) <= maxw) { font_draw(x, y, s, c, t); return; }
    float dots = font_width("...", s);
    int n = 0; float w = 0;
    while (t[n]) {
        float cw = font_width_n(t + n, 1, s);
        if (w + cw + dots > maxw) break;
        w += cw; n++;
    }
    draw_n(x, y, s, c, t, n);
    draw_n(x + w, y, s, c, "...", 3);
}

int font_draw_wrapped(float x, float y, FontSize s, Color c, const char *t, float maxw,
                      int max_lines, bool draw) {
    int lines = 0;
    float lh = font_line_pitch(s);
    const char *p = t;
    while (*p && lines < max_lines) {
        /* explicit line breaks */
        if (*p == '\n') { p++; lines++; continue; }
        const char *end = p, *last_space = NULL;
        float w = 0;
        while (*end && *end != '\n') {
            float cw = font_width_n(end, 1, s);
            if (w + cw > maxw && end > p) break;
            w += cw;
            if (*end == ' ') last_space = end;
            end++;
        }
        const char *cut = end;
        if (*end && *end != '\n' && last_space && last_space > p) cut = last_space;
        if (draw) draw_n(x, y + lines * lh, s, c, p, (int)(cut - p));
        lines++;
        p = cut;
        while (*p == ' ') p++;
        if (*p == '\n') p++;
    }
    return lines;
}
