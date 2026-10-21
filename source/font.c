/* font.c - the console's IPL font as GX textures. SYS_InitFont decompresses
 * the ROM font into one or more I4 sheets; each character is a cell at a
 * known position in one sheet, so a string is a run of textured quads. */
#include "font.h"
#include <malloc.h>
#include <string.h>

#define MAX_SHEETS 12

static sys_fontheader *hdr;
static u8 *sheet_base;
static GXTexObj sheets[MAX_SHEETS];
static int nsheets;

bool font_init(void) {
    u32 size = SYS_GetFontEncoding() == SYS_FONTENC_SJIS ? SYS_FONTSIZE_SJIS : SYS_FONTSIZE_ANSI;
    hdr = memalign(32, size);
    if (!hdr) return false;
    memset(hdr, 0, size);
    if (!SYS_InitFont(hdr)) { free(hdr); hdr = NULL; return false; }
    sheet_base = (u8 *)hdr + hdr->sheet_image;
    nsheets = hdr->sheet_size ? hdr->sheet_fullsize / hdr->sheet_size : 0;
    if (nsheets > MAX_SHEETS) nsheets = MAX_SHEETS;
    if (nsheets < 1) return false;
    DCFlushRange(hdr, size);
    for (int i = 0; i < nsheets; i++) {
        GX_InitTexObj(&sheets[i], sheet_base + i * hdr->sheet_size, hdr->sheet_width,
                      hdr->sheet_height, GX_TF_I4, GX_CLAMP, GX_CLAMP, GX_FALSE);
        GX_InitTexObjFilterMode(&sheets[i], GX_LINEAR, GX_LINEAR);
    }
    return true;
}

float font_height(float scale) { return hdr ? hdr->cell_height * scale : 16 * scale; }

static int glyph_width(int c) {
    if (!hdr) return 8;
    if (c < hdr->first_char || c > hdr->last_char) c = hdr->inval_char;
    return SYS_GetFontWidth(c);
}

float font_width_n(const char *t, int n, float scale) {
    float w = 0;
    for (int i = 0; i < n && t[i]; i++) w += glyph_width((unsigned char)t[i]) * scale;
    return w;
}

float font_width(const char *t, float scale) { return font_width_n(t, 0x7fffffff, scale); }

static void draw_n(float x, float y, float scale, Color c, const char *t, int n) {
    if (!hdr) return;
    for (int i = 0; i < n && t[i]; i++) {
        int ch = (unsigned char)t[i];
        if (ch < hdr->first_char || ch > hdr->last_char) ch = hdr->inval_char;
        void *img; s32 xpos, ypos, width;
        SYS_GetFontTexture(ch, &img, &xpos, &ypos, &width);
        int sheet = (int)(((u8 *)img - sheet_base) / hdr->sheet_size);
        if (sheet < 0 || sheet >= nsheets) sheet = 0;
        if (ch != ' ')
            gfx_tex_quad(&sheets[sheet], hdr->sheet_width, hdr->sheet_height,
                         x, y, hdr->cell_width * scale, hdr->cell_height * scale,
                         xpos, ypos, xpos + hdr->cell_width, ypos + hdr->cell_height, c);
        x += width * scale;
    }
}

void font_draw(float x, float y, float scale, Color c, const char *t) { draw_n(x, y, scale, c, t, 0x7fffffff); }

void font_draw_shadow(float x, float y, float scale, Color c, const char *t) {
    float o = scale * 2.0f;
    if (o < 1) o = 1;
    draw_n(x + o, y + o, scale, RGBA(0, 0, 0, c.a / 2), t, 0x7fffffff);
    draw_n(x, y, scale, c, t, 0x7fffffff);
}

void font_draw_center(float cx, float y, float scale, Color c, const char *t) {
    font_draw_shadow(cx - font_width(t, scale) / 2, y, scale, c, t);
}

void font_draw_right(float rx, float y, float scale, Color c, const char *t) {
    font_draw_shadow(rx - font_width(t, scale), y, scale, c, t);
}

void font_draw_clip(float x, float y, float scale, Color c, const char *t, float maxw) {
    if (font_width(t, scale) <= maxw) { font_draw_shadow(x, y, scale, c, t); return; }
    float dots = font_width("...", scale);
    int n = 0; float w = 0;
    while (t[n]) {
        float cw = glyph_width((unsigned char)t[n]) * scale;
        if (w + cw + dots > maxw) break;
        w += cw; n++;
    }
    float o = scale * 2.0f; if (o < 1) o = 1;
    draw_n(x + o, y + o, scale, RGBA(0, 0, 0, c.a / 2), t, n);
    draw_n(x + o + w, y + o, scale, RGBA(0, 0, 0, c.a / 2), "...", 3);
    draw_n(x, y, scale, c, t, n);
    draw_n(x + w, y, scale, c, "...", 3);
}

int font_draw_wrapped(float x, float y, float scale, Color c, const char *t, float maxw,
                      int max_lines, bool draw) {
    int lines = 0;
    float lh = font_height(scale) * 1.05f;
    const char *p = t;
    while (*p && lines < max_lines) {
        /* explicit line breaks */
        if (*p == '\n') { p++; lines++; continue; }
        const char *end = p, *last_space = NULL;
        float w = 0;
        while (*end && *end != '\n') {
            float cw = glyph_width((unsigned char)*end) * scale;
            if (w + cw > maxw && end > p) break;
            w += cw;
            if (*end == ' ') last_space = end;
            end++;
        }
        const char *cut = end;
        if (*end && *end != '\n' && last_space && last_space > p) cut = last_space;
        if (draw) {
            float o = scale * 2.0f; if (o < 1) o = 1;
            draw_n(x + o, y + lines * lh + o, scale, RGBA(0, 0, 0, c.a / 2), p, (int)(cut - p));
            draw_n(x, y + lines * lh, scale, c, p, (int)(cut - p));
        }
        lines++;
        p = cut;
        while (*p == ' ') p++;
        if (*p == '\n') p++;
    }
    return lines;
}
