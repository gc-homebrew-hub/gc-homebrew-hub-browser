/* gx_soft.c - just enough of GX, gu, VIDEO and the IPL font for the preview:
 * vertices go through the loaded position and projection matrices, triangles
 * are filled with perspective-correct colour and texture interpolation and
 * blended source-alpha over the frame. One 640x480 RGB frame, no depth. */
#include "gx_soft.h"
#include <gccore.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

float soft_fb[SOFT_H][SOFT_W][3], soft_shown[SOFT_H][SOFT_W][3];
int soft_enabled = 1;

static Mtx pos;
static Mtx44 proj;
static int tev_mode = GX_PASSCLR;
static GXTexObj *tex0;
static int use_tex;

typedef struct { float x, y, z, r, g, b, a, s, t; } Vert;
#define MAXV 4096
static Vert verts[MAXV];
static int nv, prim;

/* ---- matrices ---- */

void guMtxIdentity(Mtx m) {
    memset(m, 0, sizeof(Mtx));
    m[0][0] = m[1][1] = m[2][2] = 1;
}
void guMtxCopy(Mtx src, Mtx dst) { memmove(dst, src, sizeof(Mtx)); }
void guMtxConcat(Mtx a, Mtx b, Mtx ab) {
    Mtx t;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 4; j++) {
            t[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + (j == 3 ? a[i][3] : 0);
        }
    memcpy(ab, t, sizeof t);
}
void guMtxRotAxisDeg(Mtx m, guVector *ax, f32 deg) {
    float r = deg * (float)M_PI / 180.0f, c = cosf(r), s = sinf(r), t = 1 - c;
    float l = sqrtf(ax->x * ax->x + ax->y * ax->y + ax->z * ax->z);
    float x = ax->x / l, y = ax->y / l, z = ax->z / l;
    m[0][0] = t * x * x + c;     m[0][1] = t * x * y - s * z; m[0][2] = t * x * z + s * y; m[0][3] = 0;
    m[1][0] = t * x * y + s * z; m[1][1] = t * y * y + c;     m[1][2] = t * y * z - s * x; m[1][3] = 0;
    m[2][0] = t * x * z - s * y; m[2][1] = t * y * z + s * x; m[2][2] = t * z * z + c;     m[2][3] = 0;
}
void guMtxTransApply(Mtx src, Mtx dst, f32 x, f32 y, f32 z) {
    if (src != dst) memcpy(dst, src, sizeof(Mtx));
    dst[0][3] += x; dst[1][3] += y; dst[2][3] += z;
}
void guVecMultiply(Mtx m, guVector *s, guVector *d) {
    guVector t = { m[0][0] * s->x + m[0][1] * s->y + m[0][2] * s->z + m[0][3],
                   m[1][0] * s->x + m[1][1] * s->y + m[1][2] * s->z + m[1][3],
                   m[2][0] * s->x + m[2][1] * s->y + m[2][2] * s->z + m[2][3] };
    *d = t;
}
void guVecMultiplySR(Mtx m, guVector *s, guVector *d) {
    guVector t = { m[0][0] * s->x + m[0][1] * s->y + m[0][2] * s->z,
                   m[1][0] * s->x + m[1][1] * s->y + m[1][2] * s->z,
                   m[2][0] * s->x + m[2][1] * s->y + m[2][2] * s->z };
    *d = t;
}
/* the same formulas as libogc */
void guOrtho(Mtx44 m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f) {
    memset(m, 0, sizeof(Mtx44));
    m[0][0] = 2 / (r - l); m[0][3] = -(r + l) / (r - l);
    m[1][1] = 2 / (t - b); m[1][3] = -(t + b) / (t - b);
    m[2][2] = -1 / (f - n); m[2][3] = -f / (f - n);
    m[3][3] = 1;
}
void guPerspective(Mtx44 m, f32 fovy, f32 aspect, f32 n, f32 f) {
    float cot = 1.0f / tanf(fovy * 0.5f * (float)M_PI / 180.0f);
    memset(m, 0, sizeof(Mtx44));
    m[0][0] = cot / aspect;
    m[1][1] = cot;
    m[2][2] = -n / (f - n); m[2][3] = -(f * n) / (f - n);
    m[3][2] = -1;
}

/* ---- state that matters ---- */

void GX_LoadProjectionMtx(Mtx44 m, u8 type) { (void)type; memcpy(proj, m, sizeof(Mtx44)); }
void GX_LoadPosMtxImm(Mtx m, u32 idx) { (void)idx; memcpy(pos, m, sizeof(Mtx)); }
void GX_LoadTexObj(GXTexObj *o, u8 map) { (void)map; tex0 = o; }
void GX_SetTevOp(u8 stage, u8 mode) { (void)stage; tev_mode = mode; }
void GX_SetVtxDesc(u8 attr, u8 type) { if (attr == GX_VA_TEX0) use_tex = type != GX_NONE; }
void GX_ClearVtxDesc(void) { use_tex = 0; }
void GX_InitTexObj(GXTexObj *o, void *img, u16 w, u16 h, u8 fmt, u8 ws, u8 wt, u8 mip) {
    (void)ws; (void)wt; (void)mip;
    o->img = img; o->w = w; o->h = h; o->fmt = fmt; o->filter = GX_LINEAR;
}
void GX_InitTexObjFilterMode(GXTexObj *o, u8 minf, u8 magf) { (void)minf; o->filter = magf; }

/* ---- everything else is accepted and ignored ---- */

void GX_Init(void *fifo, u32 size) { (void)fifo; (void)size; }
void GX_SetCopyClear(GXColor c, u32 z) { (void)c; (void)z; }
void GX_SetViewport(f32 x, f32 y, f32 w, f32 h, f32 n, f32 f) { (void)x; (void)y; (void)w; (void)h; (void)n; (void)f; }
f32 GX_GetYScaleFactor(u16 efbh, u16 xfbh) { (void)efbh; (void)xfbh; return 1; }
u32 GX_SetDispCopyYScale(f32 s) { (void)s; return SOFT_H; }
void GX_SetScissor(u32 x, u32 y, u32 w, u32 h) { (void)x; (void)y; (void)w; (void)h; }
void GX_SetDispCopySrc(u16 l, u16 t, u16 w, u16 h) { (void)l; (void)t; (void)w; (void)h; }
void GX_SetDispCopyDst(u16 w, u16 h) { (void)w; (void)h; }
void GX_SetCopyFilter(u8 aa, u8 p[12][2], u8 vf, u8 v[7]) { (void)aa; (void)p; (void)vf; (void)v; }
void GX_SetFieldMode(u8 f, u8 h) { (void)f; (void)h; }
void GX_SetPixelFmt(u8 p, u8 z) { (void)p; (void)z; }
void GX_SetCullMode(u8 m) { (void)m; }
void GX_SetDispCopyGamma(u8 g) { (void)g; }
void GX_SetVtxAttrFmt(u8 f, u32 a, u32 c, u32 t, u32 fr) { (void)f; (void)a; (void)c; (void)t; (void)fr; }
void GX_SetNumChans(u8 n) { (void)n; }
void GX_SetChanCtrl(s32 ch, u8 en, u8 amb, u8 mat, u8 l, u8 d, u8 a) { (void)ch; (void)en; (void)amb; (void)mat; (void)l; (void)d; (void)a; }
void GX_SetNumTevStages(u8 n) { (void)n; }
void GX_SetNumTexGens(u32 n) { (void)n; }
void GX_SetTexCoordGen(u16 tc, u32 t, u32 s, u32 m) { (void)tc; (void)t; (void)s; (void)m; }
void GX_SetBlendMode(u8 t, u8 s, u8 d, u8 o) { (void)t; (void)s; (void)d; (void)o; }
void GX_SetAlphaUpdate(u8 e) { (void)e; }
void GX_SetColorUpdate(u8 e) { (void)e; }
void GX_SetZMode(u8 e, u8 f, u8 u) { (void)e; (void)f; (void)u; }
void GX_InvVtxCache(void) {}
void GX_InvalidateTexAll(void) {}
void GX_DrawDone(void) {}
void GX_SetTevOrder(u8 s, u8 c, u32 m, u8 col) { (void)s; (void)c; (void)m; (void)col; }
void GX_CopyDisp(void *dst, u8 clear) {
    (void)dst;
    if (soft_enabled) memcpy(soft_shown, soft_fb, sizeof soft_fb);
    if (clear) memset(soft_fb, 0, sizeof soft_fb);
}

static GXRModeObj mode = { .fbWidth = SOFT_W, .efbHeight = SOFT_H, .xfbHeight = SOFT_H, .viHeight = SOFT_H };
void VIDEO_Init(void) {}
GXRModeObj *VIDEO_GetPreferredMode(GXRModeObj *m) { (void)m; return &mode; }
void VIDEO_ClearFrameBuffer(GXRModeObj *m, void *fb, u32 c) { (void)m; (void)fb; (void)c; }
void VIDEO_Configure(GXRModeObj *m) { (void)m; }
void VIDEO_SetNextFramebuffer(void *fb) { (void)fb; }
void VIDEO_SetBlack(bool b) { (void)b; }
void VIDEO_Flush(void) {}
void VIDEO_WaitVSync(void) {}
void *SYS_AllocateFramebuffer(GXRModeObj *m) { (void)m; static char fb[16]; return fb; }
void DCFlushRange(void *p, u32 len) { (void)p; (void)len; }
void SYS_ResetSystem(s32 r, u32 t, s32 f) { (void)r; (void)t; (void)f; }

/* ---- vertices ---- */

void GX_Begin(u8 p, u8 fmt, u16 n) { (void)fmt; (void)n; prim = p; nv = 0; }
void GX_Position3f32(f32 x, f32 y, f32 z) {
    if (nv >= MAXV) return;
    Vert *v = &verts[nv++];
    memset(v, 0, sizeof *v);
    v->x = x; v->y = y; v->z = z;
}
void GX_Color4u8(u8 r, u8 g, u8 b, u8 a) {
    if (!nv) return;
    Vert *v = &verts[nv - 1];
    v->r = r / 255.0f; v->g = g / 255.0f; v->b = b / 255.0f; v->a = a / 255.0f;
}
void GX_TexCoord2f32(f32 s, f32 t) {
    if (!nv) return;
    verts[nv - 1].s = s; verts[nv - 1].t = t;
}

/* a vertex after the matrices: screen position, 1/w and attributes over w */
typedef struct { float sx, sy, iw, a[6]; } SV;

static int transform(const Vert *v, SV *o) {
    float e[3];
    for (int i = 0; i < 3; i++) e[i] = pos[i][0] * v->x + pos[i][1] * v->y + pos[i][2] * v->z + pos[i][3];
    float c[4];
    for (int i = 0; i < 4; i++) c[i] = proj[i][0] * e[0] + proj[i][1] * e[1] + proj[i][2] * e[2] + proj[i][3];
    if (c[3] < 1e-4f) return 0;
    float iw = 1.0f / c[3];
    o->sx = (c[0] * iw + 1) * 0.5f * SOFT_W;
    o->sy = (1 - c[1] * iw) * 0.5f * SOFT_H;
    o->iw = iw;
    float at[6] = { v->r, v->g, v->b, v->a, v->s, v->t };
    for (int i = 0; i < 6; i++) o->a[i] = at[i] * iw;
    return 1;
}

static float sample(float s, float t) {
    const GXTexObj *o = tex0;
    if (!o || !o->img) return 1;
    const u8 *img = o->img;
    float x = s * o->w - 0.5f, y = t * o->h - 0.5f;
    int x0 = (int)floorf(x), y0 = (int)floorf(y);
    float fx = x - x0, fy = y - y0;
#define PX(xx, yy) (img[((yy) < 0 ? 0 : (yy) >= o->h ? o->h - 1 : (yy)) * o->w + \
                         ((xx) < 0 ? 0 : (xx) >= o->w ? o->w - 1 : (xx))] / 255.0f)
    if (o->filter == GX_NEAR) return PX((int)(s * o->w), (int)(t * o->h));
    return (PX(x0, y0) * (1 - fx) + PX(x0 + 1, y0) * fx) * (1 - fy) +
           (PX(x0, y0 + 1) * (1 - fx) + PX(x0 + 1, y0 + 1) * fx) * fy;
#undef PX
}

static void tri(const SV *a, const SV *b, const SV *c) {
    float area = (b->sx - a->sx) * (c->sy - a->sy) - (b->sy - a->sy) * (c->sx - a->sx);
    if (fabsf(area) < 1e-6f) return;
    int x0 = (int)floorf(fminf(a->sx, fminf(b->sx, c->sx))), x1 = (int)ceilf(fmaxf(a->sx, fmaxf(b->sx, c->sx)));
    int y0 = (int)floorf(fminf(a->sy, fminf(b->sy, c->sy))), y1 = (int)ceilf(fmaxf(a->sy, fmaxf(b->sy, c->sy)));
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > SOFT_W) x1 = SOFT_W;
    if (y1 > SOFT_H) y1 = SOFT_H;
    bool textured = use_tex && tev_mode == GX_MODULATE;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float w0 = ((b->sx - px) * (c->sy - py) - (b->sy - py) * (c->sx - px)) / area;
            float w1 = ((c->sx - px) * (a->sy - py) - (c->sy - py) * (a->sx - px)) / area;
            float w2 = 1 - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            float iw = w0 * a->iw + w1 * b->iw + w2 * c->iw;
            float at[6];
            for (int i = 0; i < 6; i++) at[i] = (w0 * a->a[i] + w1 * b->a[i] + w2 * c->a[i]) / iw;
            float r = at[0], g = at[1], bl = at[2], al = at[3];
            if (textured) {
                float in = sample(at[4], at[5]);
                r *= in; g *= in; bl *= in; al *= in;
            }
            if (al <= 0) continue;
            if (al > 1) al = 1;
            float *d = soft_fb[y][x];
            d[0] = r * al + d[0] * (1 - al);
            d[1] = g * al + d[1] * (1 - al);
            d[2] = bl * al + d[2] * (1 - al);
        }
}

void GX_End(void) {
    if (!soft_enabled) return;
    static SV sv[MAXV];
    static int ok[MAXV];
    for (int i = 0; i < nv; i++) ok[i] = transform(&verts[i], &sv[i]);
#define T(i, j, k) do { if (ok[i] && ok[j] && ok[k]) tri(&sv[i], &sv[j], &sv[k]); } while (0)
    switch (prim) {
    case GX_QUADS: for (int i = 0; i + 3 < nv; i += 4) { T(i, i + 1, i + 2); T(i, i + 2, i + 3); } break;
    case GX_TRIANGLES: for (int i = 0; i + 2 < nv; i += 3) T(i, i + 1, i + 2); break;
    case GX_TRIANGLESTRIP: for (int i = 0; i + 2 < nv; i++) T(i, i + 1, i + 2); break;
    case GX_TRIANGLEFAN: for (int i = 1; i + 1 < nv; i++) T(0, i, i + 1); break;
    default: break;
    }
#undef T
}

/* ---- the font: a sheet made by font_atlas.py from any TrueType font,
 * standing in for the console's ROM font ---- */

static struct { int cw, ch, first, last, cols, sw, sh; u8 widths[256]; u8 *pix; } atlas;

int soft_load_font(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int hdr[7];
    if (fread(hdr, sizeof hdr, 1, f) != 1) { fclose(f); return 0; }
    atlas.cw = hdr[0]; atlas.ch = hdr[1]; atlas.first = hdr[2]; atlas.last = hdr[3];
    atlas.cols = hdr[4]; atlas.sw = hdr[5]; atlas.sh = hdr[6];
    atlas.pix = malloc((size_t)atlas.sw * atlas.sh);
    int ok = fread(atlas.widths, 256, 1, f) == 1 && fread(atlas.pix, (size_t)atlas.sw * atlas.sh, 1, f) == 1;
    fclose(f);
    return ok;
}

static sys_fontheader *font_hdr;

u32 SYS_GetFontEncoding(void) { return SYS_FONTENC_ANSI; }

u32 SYS_InitFont(sys_fontheader *h) {
    if (!atlas.pix) return 0;
    size_t img = (sizeof *h + 31) & ~31u, size = (size_t)atlas.sw * atlas.sh;
    if (img + size > SYS_FONTSIZE_ANSI) return 0;
    memset(h, 0, sizeof *h);
    h->first_char = atlas.first; h->last_char = atlas.last; h->inval_char = '?';
    h->cell_width = atlas.cw; h->cell_height = atlas.ch;
    h->sheet_width = atlas.sw; h->sheet_height = atlas.sh;
    h->sheet_column = atlas.cols;
    h->sheet_size = h->sheet_fullsize = size;
    h->sheet_image = img;
    memcpy((u8 *)h + img, atlas.pix, size);
    font_hdr = h;
    return 1;
}

s32 SYS_GetFontWidth(s32 c) { return atlas.widths[c & 255]; }

void SYS_GetFontTexture(s32 c, void **image, s32 *xpos, s32 *ypos, s32 *width) {
    int i = c - atlas.first;
    *image = (u8 *)font_hdr + font_hdr->sheet_image;
    *xpos = (i % atlas.cols) * atlas.cw;
    *ypos = (i / atlas.cols) * atlas.ch;
    *width = atlas.widths[c & 255];
}
