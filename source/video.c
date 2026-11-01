/* video.c - GX initialisation and the drawing primitives everything is built from. */
#include "video.h"
#include <malloc.h>
#include <math.h>
#include <string.h>

#define FIFO_SIZE (256 * 1024)

static GXRModeObj *rmode;
static void *xfb[2];
static int cur_fb;
static int frames;
static bool widescreen;
static Mtx44 ortho;
static Mtx identity;

void video_init(void) {
    VIDEO_Init();
    rmode = VIDEO_GetPreferredMode(NULL);
    widescreen = false;

    xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    VIDEO_ClearFrameBuffer(rmode, xfb[0], COLOR_BLACK);
    VIDEO_ClearFrameBuffer(rmode, xfb[1], COLOR_BLACK);

    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb[0]);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();

    void *fifo = memalign(32, FIFO_SIZE);
    memset(fifo, 0, FIFO_SIZE);
    GX_Init(fifo, FIFO_SIZE);

    GXColor clear = { 0, 0, 0, 255 };
    GX_SetCopyClear(clear, 0x00ffffff);
    GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
    f32 yscale = GX_GetYScaleFactor(rmode->efbHeight, rmode->xfbHeight);
    u32 xfbh = GX_SetDispCopyYScale(yscale);
    GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopyDst(rmode->fbWidth, xfbh);
    GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
    GX_SetFieldMode(rmode->field_rendering,
                    (rmode->viHeight == 2 * rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE);
    if (rmode->aa) GX_SetPixelFmt(GX_PF_RGB565_Z16, GX_ZC_LINEAR);
    else           GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetDispCopyGamma(GX_GM_1_0);

    GX_ClearVtxDesc();
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
    GX_SetNumTevStages(1);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GX_SetAlphaUpdate(GX_TRUE);
    GX_InvVtxCache();
    GX_InvalidateTexAll();

    guOrtho(ortho, 0, SCREEN_H, 0, SCREEN_W, 0, 1000);
    guMtxIdentity(identity);
    GX_CopyDisp(xfb[0], GX_TRUE);
}

bool video_is_widescreen(void) { return widescreen; }
int video_frame_count(void) { return frames; }

static void mode_2d(void) {
    GX_LoadProjectionMtx(ortho, GX_ORTHOGRAPHIC);
    GX_LoadPosMtxImm(identity, GX_PNMTX0);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetCullMode(GX_CULL_NONE);
}

static void flat(void) {
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetNumTexGens(0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
}

static void textured(GXTexObj *tex) {
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetNumTexGens(1);
    GX_LoadTexObj(tex, GX_TEXMAP0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
}

void video_frame_begin(void) {
    mode_2d();
    flat();
}

void video_frame_end(void) {
    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    GX_SetColorUpdate(GX_TRUE);
    cur_fb ^= 1;
    GX_CopyDisp(xfb[cur_fb], GX_TRUE);
    GX_DrawDone();
    VIDEO_SetNextFramebuffer(xfb[cur_fb]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    frames++;
}

static u8 opacity = 255;
void gfx_set_opacity(float a) { opacity = a <= 0 ? 0 : a >= 1 ? 255 : (u8)(a * 255 + 0.5f); }
static inline u8 fade(u8 a) { return opacity == 255 ? a : (u8)((a * opacity + 127) / 255); }

static inline void vtx(float x, float y, Color c) {
    GX_Position3f32(x, y, 0);
    GX_Color4u8(c.r, c.g, c.b, fade(c.a));
}

void gfx_rect(float x, float y, float w, float h, Color c) {
    if (w <= 0 || h <= 0 || c.a == 0) return;
    flat();
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    vtx(x, y, c); vtx(x + w, y, c); vtx(x + w, y + h, c); vtx(x, y + h, c);
    GX_End();
}

void gfx_rect_gradient(float x, float y, float w, float h, Color top, Color bottom) {
    if (w <= 0 || h <= 0) return;
    flat();
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    vtx(x, y, top); vtx(x + w, y, top); vtx(x + w, y + h, bottom); vtx(x, y + h, bottom);
    GX_End();
}

void gfx_rect_hgradient(float x, float y, float w, float h, Color left, Color right) {
    if (w <= 0 || h <= 0) return;
    flat();
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    vtx(x, y, left); vtx(x + w, y, right); vtx(x + w, y + h, right); vtx(x, y + h, left);
    GX_End();
}

void gfx_strip(const float *xy, const Color *c, int n) {
    if (n < 3) return;
    flat();
    GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, n);
    for (int i = 0; i < n; i++) vtx(xy[2 * i], xy[2 * i + 1], c[i]);
    GX_End();
}

static inline Color mix_y(Color top, Color bottom, float t) {
    return RGBA((u8)(top.r + (bottom.r - top.r) * t), (u8)(top.g + (bottom.g - top.g) * t),
                (u8)(top.b + (bottom.b - top.b) * t), (u8)(top.a + (bottom.a - top.a) * t));
}

/* A fan from the centre; the colour follows y, which a fan interpolates exactly. */
void gfx_round_rect_gradient(float x, float y, float w, float h, float r, Color top, Color bottom) {
    if (w <= 0 || h <= 0 || (top.a == 0 && bottom.a == 0)) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 1) { gfx_rect_gradient(x, y, w, h, top, bottom); return; }
    const int seg = r < 8 ? 3 : r < 24 ? 6 : 10;
    flat();
    GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, 2 + 4 * (seg + 1));
    vtx(x + w / 2, y + h / 2, mix_y(top, bottom, 0.5f));
    /* corners in order: top-left, top-right, bottom-right, bottom-left */
    const float cx[4] = { x + r, x + w - r, x + w - r, x + r };
    const float cy[4] = { y + r, y + r, y + h - r, y + h - r };
    for (int k = 0; k < 4; k++) {
        float a0 = (float)M_PI * (1.0f + 0.5f * k);
        for (int i = 0; i <= seg; i++) {
            float a = a0 + (float)M_PI / 2 * i / seg;
            float py = cy[k] + r * sinf(a);
            vtx(cx[k] + r * cosf(a), py, mix_y(top, bottom, (py - y) / h));
        }
    }
    vtx(x, y + r, mix_y(top, bottom, r / h));
    GX_End();
}

void gfx_round_rect(float x, float y, float w, float h, float r, Color c) {
    gfx_round_rect_gradient(x, y, w, h, r, c, c);
}

void gfx_circle(float cx, float cy, float r, Color c) {
    gfx_round_rect(cx - r, cy - r, 2 * r, 2 * r, r, c);
}

void gfx_poly(const float *xy, int n, Color c) {
    if (n < 3 || c.a == 0) return;
    flat();
    GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, n);
    for (int i = 0; i < n; i++) vtx(xy[2 * i], xy[2 * i + 1], c);
    GX_End();
}

void gfx_frame(float x, float y, float w, float h, float t, Color c) {
    gfx_rect(x, y, w, t, c);
    gfx_rect(x, y + h - t, w, t, c);
    gfx_rect(x, y + t, t, h - 2 * t, c);
    gfx_rect(x + w - t, y + t, t, h - 2 * t, c);
}

void gfx_line(float x0, float y0, float x1, float y1, float width, Color c) {
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.01f) return;
    float nx = -dy / len * width / 2, ny = dx / len * width / 2;
    flat();
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    vtx(x0 + nx, y0 + ny, c); vtx(x1 + nx, y1 + ny, c);
    vtx(x1 - nx, y1 - ny, c); vtx(x0 - nx, y0 - ny, c);
    GX_End();
}

void gfx_tex_quad(GXTexObj *tex, float tw, float th, float x, float y, float w, float h,
                  float s0, float t0, float s1, float t1, Color tint) {
    tint.a = fade(tint.a);
    textured(tex);
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position3f32(x, y, 0);         GX_Color4u8(tint.r, tint.g, tint.b, tint.a); GX_TexCoord2f32(s0 / tw, t0 / th);
    GX_Position3f32(x + w, y, 0);     GX_Color4u8(tint.r, tint.g, tint.b, tint.a); GX_TexCoord2f32(s1 / tw, t0 / th);
    GX_Position3f32(x + w, y + h, 0); GX_Color4u8(tint.r, tint.g, tint.b, tint.a); GX_TexCoord2f32(s1 / tw, t1 / th);
    GX_Position3f32(x, y + h, 0);     GX_Color4u8(tint.r, tint.g, tint.b, tint.a); GX_TexCoord2f32(s0 / tw, t1 / th);
    GX_End();
}
