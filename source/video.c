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

static inline void vtx(float x, float y, Color c) {
    GX_Position3f32(x, y, 0);
    GX_Color4u8(c.r, c.g, c.b, c.a);
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

void gfx_round_rect(float x, float y, float w, float h, float r, Color c) {
    if (w <= 0 || h <= 0 || c.a == 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 1) { gfx_rect(x, y, w, h, c); return; }
    const int seg = 6;
    flat();
    GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, 2 + 4 * (seg + 1));
    vtx(x + w / 2, y + h / 2, c);
    /* corners in order: top-left, top-right, bottom-right, bottom-left */
    const float cx[4] = { x + r, x + w - r, x + w - r, x + r };
    const float cy[4] = { y + r, y + r, y + h - r, y + h - r };
    for (int k = 0; k < 4; k++) {
        float a0 = (float)M_PI * (1.0f + 0.5f * k);
        for (int i = 0; i <= seg; i++) {
            float a = a0 + (float)M_PI / 2 * i / seg;
            vtx(cx[k] + r * cosf(a), cy[k] + r * sinf(a), c);
        }
    }
    vtx(x, y + r, c);
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
    textured(tex);
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position3f32(x, y, 0);         GX_Color4u8(tint.r, tint.g, tint.b, tint.a); GX_TexCoord2f32(s0 / tw, t0 / th);
    GX_Position3f32(x + w, y, 0);     GX_Color4u8(tint.r, tint.g, tint.b, tint.a); GX_TexCoord2f32(s1 / tw, t0 / th);
    GX_Position3f32(x + w, y + h, 0); GX_Color4u8(tint.r, tint.g, tint.b, tint.a); GX_TexCoord2f32(s1 / tw, t1 / th);
    GX_Position3f32(x, y + h, 0);     GX_Color4u8(tint.r, tint.g, tint.b, tint.a); GX_TexCoord2f32(s0 / tw, t1 / th);
    GX_End();
}

/* The cube is drawn with a perspective projection whose frustum is sized so
 * that z = -D maps 1:1 onto logical pixels; the shading is computed on the
 * CPU from one directional light, so no GX lighting state is needed. */
void gfx_cube(float cx, float cy, float size, float rx, float ry, float rz, Color base, float alpha) {
    const float D = 600.0f;                         /* camera distance */
    Mtx44 proj;
    guPerspective(proj, 2.0f * atanf((SCREEN_H / 2.0f) / D) * 180.0f / (float)M_PI,
                  (float)SCREEN_W / SCREEN_H, 10.0f, 2000.0f);
    GX_LoadProjectionMtx(proj, GX_PERSPECTIVE);

    Mtx rot, tmp, model;
    guVector ax = { 1, 0, 0 }, ay = { 0, 1, 0 }, az = { 0, 0, 1 };
    guMtxIdentity(model);
    guMtxRotAxisDeg(tmp, &ay, ry); guMtxConcat(tmp, model, model);
    guMtxRotAxisDeg(tmp, &ax, rx); guMtxConcat(tmp, model, model);
    guMtxRotAxisDeg(tmp, &az, rz); guMtxConcat(tmp, model, model);
    guMtxCopy(model, rot);
    guMtxTransApply(model, model, cx - SCREEN_W / 2.0f, SCREEN_H / 2.0f - cy, -D);
    GX_LoadPosMtxImm(model, GX_PNMTX0);

    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    GX_SetCullMode(GX_CULL_BACK);
    flat();

    static const float n[6][3] = { {0,0,1}, {0,0,-1}, {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0} };
    /* vertices of each face, counter-clockwise seen from outside */
    static const float f[6][4][3] = {
        { {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1} },   /* front  */
        { { 1,-1,-1}, {-1,-1,-1}, {-1, 1,-1}, { 1, 1,-1} },   /* back   */
        { { 1,-1, 1}, { 1,-1,-1}, { 1, 1,-1}, { 1, 1, 1} },   /* right  */
        { {-1,-1,-1}, {-1,-1, 1}, {-1, 1, 1}, {-1, 1,-1} },   /* left   */
        { {-1, 1, 1}, { 1, 1, 1}, { 1, 1,-1}, {-1, 1,-1} },   /* top    */
        { {-1,-1,-1}, { 1,-1,-1}, { 1,-1, 1}, {-1,-1, 1} },   /* bottom */
    };
    const float lx = -0.45f, ly = 0.6f, lz = 0.66f;   /* light from the upper left front */
    GX_Begin(GX_QUADS, GX_VTXFMT0, 24);
    for (int i = 0; i < 6; i++) {
        guVector nn = { n[i][0], n[i][1], n[i][2] }, wn;
        guVecMultiplySR(rot, &nn, &wn);
        float d = wn.x * lx + wn.y * ly + wn.z * lz;
        float lit = 0.45f + 0.55f * (d > 0 ? d : 0);
        float spec = d > 0.92f ? (d - 0.92f) * 6.0f : 0;
        Color c = RGBA((u8)fminf(255, base.r * lit + 255 * spec),
                       (u8)fminf(255, base.g * lit + 255 * spec),
                       (u8)fminf(255, base.b * lit + 255 * spec),
                       (u8)(alpha * 255));
        for (int v = 0; v < 4; v++) {
            GX_Position3f32(f[i][v][0] * size, f[i][v][1] * size, f[i][v][2] * size);
            GX_Color4u8(c.r, c.g, c.b, c.a);
        }
    }
    GX_End();
    mode_2d();
}
