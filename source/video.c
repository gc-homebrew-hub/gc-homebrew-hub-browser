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
    textured(tex);
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position3f32(x, y, 0);         GX_Color4u8(tint.r, tint.g, tint.b, tint.a); GX_TexCoord2f32(s0 / tw, t0 / th);
    GX_Position3f32(x + w, y, 0);     GX_Color4u8(tint.r, tint.g, tint.b, tint.a); GX_TexCoord2f32(s1 / tw, t0 / th);
    GX_Position3f32(x + w, y + h, 0); GX_Color4u8(tint.r, tint.g, tint.b, tint.a); GX_TexCoord2f32(s1 / tw, t1 / th);
    GX_Position3f32(x, y + h, 0);     GX_Color4u8(tint.r, tint.g, tint.b, tint.a); GX_TexCoord2f32(s0 / tw, t1 / th);
    GX_End();
}

/* ---- the glass cube ----
 *
 * Drawn with a perspective projection whose frustum is sized so that z = -D
 * maps 1:1 onto logical pixels. Shading is computed on the CPU from one
 * directional light, so no GX lighting state is needed. A cube is convex, so
 * its far faces, its near faces and the faces of the core never overlap
 * among themselves: painting far faces, core, near faces in that order needs
 * no depth buffer and no culling. */

#define CAM_D 600.0f

static const float cube_n[6][3] = { {0,0,1}, {0,0,-1}, {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0} };
/* each face's right (u) and up (v) directions, as seen from outside */
static const float cube_u[6][3] = { {1,0,0}, {-1,0,0}, {0,0,-1}, {0,0,1}, {1,0,0}, {1,0,0} };
static const float cube_v[6][3] = { {0,1,0}, {0,1,0}, {0,1,0}, {0,1,0}, {0,0,-1}, {0,0,1} };
static const float light[3] = { -0.45f, 0.6f, 0.66f };   /* from the upper left front */

static void cube_projection(void) {
    Mtx44 proj;
    guPerspective(proj, 2.0f * atanf((SCREEN_H / 2.0f) / CAM_D) * 180.0f / (float)M_PI,
                  (float)SCREEN_W / SCREEN_H, 10.0f, 2000.0f);
    GX_LoadProjectionMtx(proj, GX_PERSPECTIVE);
}

static void rotation(Mtx out, float rx, float ry, float rz) {
    Mtx tmp;
    guVector ax = { 1, 0, 0 }, ay = { 0, 1, 0 }, az = { 0, 0, 1 };
    guMtxIdentity(out);
    guMtxRotAxisDeg(tmp, &ay, ry); guMtxConcat(tmp, out, out);
    guMtxRotAxisDeg(tmp, &ax, rx); guMtxConcat(tmp, out, out);
    guMtxRotAxisDeg(tmp, &az, rz); guMtxConcat(tmp, out, out);
}

static void project(const guVector *w, float *sx, float *sy) {
    float k = CAM_D / (w->z < -1.0f ? -w->z : 1.0f);
    *sx = SCREEN_W / 2.0f + w->x * k;
    *sy = SCREEN_H / 2.0f - w->y * k;
}

static inline float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static Color shade(Color c, float k, float add, float a) {
    return RGBA((u8)fminf(255, c.r * k + add), (u8)fminf(255, c.g * k + add),
                (u8)fminf(255, c.b * k + add), (u8)(255 * clamp01(a)));
}

/* How much each face of a cube with matrix m looks at the camera (<= 0: away). */
static void facing(Mtx m, float size, float out[6]) {
    for (int i = 0; i < 6; i++) {
        guVector n = { cube_n[i][0], cube_n[i][1], cube_n[i][2] }, wn, c;
        guVector p = { n.x * size, n.y * size, n.z * size };
        guVecMultiplySR(m, &n, &wn);
        guVecMultiply(m, &p, &c);
        float len = sqrtf(c.x * c.x + c.y * c.y + c.z * c.z);
        out[i] = len > 0 ? -(wn.x * c.x + wn.y * c.y + wn.z * c.z) / len : 0;
    }
}

/* One face as a quad; the top of the face (in world y) gets the sheen. */
static void face_quad(Mtx m, int i, float size, Color c, float alpha, float gloss) {
    guVector n = { cube_n[i][0], cube_n[i][1], cube_n[i][2] }, wn;
    guVecMultiplySR(m, &n, &wn);
    float d = wn.x * light[0] + wn.y * light[1] + wn.z * light[2];
    float lit = 0.32f + 0.6f * (d > 0 ? d : 0);
    float spec = d > 0.9f ? (d - 0.9f) * 5.0f : 0;
    static const float su[4] = { -1, 1, 1, -1 }, sv[4] = { 1, 1, -1, -1 };
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    for (int k = 0; k < 4; k++) {
        float p[3];
        for (int j = 0; j < 3; j++)
            p[j] = (cube_n[i][j] + su[k] * cube_u[i][j] + sv[k] * cube_v[i][j]) * size;
        guVector pv = { p[0], p[1], p[2] }, wp;
        guVecMultiplySR(m, &pv, &wp);
        float sheen = clamp01(0.5f + 0.5f * wp.y / (size * 1.5f));
        Color v = shade(c, lit, 255 * (spec + gloss * sheen * sheen), alpha * (0.75f + 0.4f * sheen));
        GX_Position3f32(p[0], p[1], p[2]);
        GX_Color4u8(v.r, v.g, v.b, v.a);
    }
    GX_End();
}

static void edges(Mtx m, float size, const float face[6], bool front, Color c, float alpha) {
    guVector w[8];
    float sx[8], sy[8];
    for (int k = 0; k < 8; k++) {
        guVector p = { (k & 1 ? 1 : -1) * size, (k & 2 ? 1 : -1) * size, (k & 4 ? 1 : -1) * size };
        guVecMultiply(m, &p, &w[k]);
        project(&w[k], &sx[k], &sy[k]);
    }
    /* face index for (axis, sign): x+ right, x- left, y+ top, y- bottom, z+ front, z- back */
    static const int face_of[3][2] = { { CUBE_LEFT, CUBE_RIGHT }, { CUBE_BOTTOM, CUBE_TOP }, { CUBE_BACK, CUBE_FRONT } };
    for (int a = 0; a < 8; a++)
        for (int bit = 1; bit < 8; bit <<= 1) {
            int b = a | bit;
            if (b == a) continue;
            bool near = false;
            for (int axis = 0; axis < 3; axis++)
                if ((1 << axis) != bit && face[face_of[axis][(a >> axis) & 1]] > 0) near = true;
            if (near != front) continue;
            float k = front ? 1.0f : 0.35f;
            gfx_line(sx[a], sy[a], sx[b], sy[b], 6, RGBA(c.r, c.g, c.b, (u8)(40 * k * alpha)));
            gfx_line(sx[a], sy[a], sx[b], sy[b], 1.6f, RGBA(c.r, c.g, c.b, (u8)(230 * k * alpha)));
        }
    if (!front) return;
    for (int k = 0; k < 8; k++) {
        bool near = face[k & 1 ? CUBE_RIGHT : CUBE_LEFT] > 0 || face[k & 2 ? CUBE_TOP : CUBE_BOTTOM] > 0 ||
                    face[k & 4 ? CUBE_FRONT : CUBE_BACK] > 0;
        if (!near) continue;
        gfx_rect(sx[k] - 3, sy[k] - 3, 6, 6, RGBA(c.r, c.g, c.b, (u8)(60 * alpha)));
        gfx_rect(sx[k] - 1.5f, sy[k] - 1.5f, 3, 3, RGBA(255, 255, 255, (u8)(220 * alpha)));
    }
}

/* Loads a matrix that maps face units (0..100, y down) onto face i. */
static void face_space(Mtx m, int i, float size) {
    Mtx local, out;
    const float k = size / 50.0f;
    for (int j = 0; j < 3; j++) {
        local[j][0] = cube_u[i][j] * k;
        local[j][1] = -cube_v[i][j] * k;
        local[j][2] = cube_n[i][j] * k;
        local[j][3] = (cube_n[i][j] * 1.002f - cube_u[i][j] + cube_v[i][j]) * size;
    }
    guMtxConcat(m, local, out);
    GX_LoadPosMtxImm(out, GX_PNMTX0);
}

void gfx_glass_cube(float cx, float cy, float size, float rx, float ry, float rz, const CubeStyle *st) {
    Mtx model;
    rotation(model, rx, ry, rz);
    guMtxTransApply(model, model, cx - SCREEN_W / 2.0f, SCREEN_H / 2.0f - cy, -CAM_D);
    float face[6];
    facing(model, size, face);
    float a = st->alpha;

    cube_projection();
    GX_LoadPosMtxImm(model, GX_PNMTX0);
    flat();
    /* far faces: the inside of the glass, darker */
    for (int i = 0; i < 6; i++)
        if (face[i] <= 0) face_quad(model, i, size, shade(st->glass, 0.55f, 0, 1), a * 0.45f, 0);
    mode_2d();
    edges(model, size, face, false, st->edge, a);

    /* the core */
    if (st->core.a) {
        Mtx spin, core;
        rotation(spin, st->core_spin * 0.6f, st->core_spin, 0);
        guMtxConcat(model, spin, core);
        float cs = size * 0.42f, cf[6];
        facing(core, cs, cf);
        cube_projection();
        GX_LoadPosMtxImm(core, GX_PNMTX0);
        flat();
        for (int i = 0; i < 6; i++)
            if (cf[i] > 0) face_quad(core, i, cs, st->core, a * st->core.a / 255.0f, 0.15f);
    }

    /* near faces with their gloss and decals */
    cube_projection();
    for (int i = 0; i < 6; i++) {
        if (face[i] <= 0) continue;
        GX_LoadPosMtxImm(model, GX_PNMTX0);
        flat();
        face_quad(model, i, size, st->glass, a * 0.5f, 0.35f);
        face_space(model, i, size);
        float f = clamp01(face[i] * 1.6f);
        /* the glossy top half, the Y2K way */
        gfx_rect_gradient(3, 3, 94, 46, RGBA(255, 255, 255, (u8)(70 * a * f)), RGBA(255, 255, 255, 0));
        if (st->face) st->face(i, f * a, st->user);
    }
    mode_2d();
    edges(model, size, face, true, st->edge, a);
}

void gfx_cube(float x, float y, float size, float rx, float ry, float rz, Color base, float alpha) {
    CubeStyle st = { base, RGBA(0, 0, 0, 0), RGB(200, 190, 255), alpha, 0, NULL, NULL };
    gfx_glass_cube(x, y, size, rx, ry, rz, &st);
}
