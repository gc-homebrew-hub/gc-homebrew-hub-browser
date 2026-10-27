/* gccore.h - host stand-in for the parts of libogc2 the drawing code uses.
 * gx_soft.c implements them with a small software rasterizer so the UI can
 * be rendered to images on a PC (preview only, not the console's behaviour
 * in every detail: no depth buffer, no culling, no anti-aliasing). */
#pragma once
#include "gctypes.h"
#include <stddef.h>

#define GX_FALSE 0
#define GX_TRUE  1
#define GX_DISABLE 0
#define GX_ENABLE  1

typedef f32 Mtx[3][4];
typedef f32 Mtx44[4][4];
typedef struct { f32 x, y, z; } guVector;
typedef struct { u8 r, g, b, a; } GXColor;
typedef struct { void *img; u16 w, h; u8 fmt, filter; } GXTexObj;

typedef struct {
    u32 viTVMode;
    u16 fbWidth, efbHeight, xfbHeight;
    u16 viXOrigin, viYOrigin, viWidth, viHeight;
    u32 xfbMode;
    u8 field_rendering, aa;
    u8 sample_pattern[12][2];
    u8 vfilter[7];
} GXRModeObj;

/* primitives and vertex formats */
enum { GX_QUADS = 0x80, GX_TRIANGLES = 0x90, GX_TRIANGLESTRIP = 0x98, GX_TRIANGLEFAN = 0xA0,
       GX_LINES = 0xA8, GX_LINESTRIP = 0xB0, GX_POINTS = 0xB8 };
enum { GX_VTXFMT0 = 0 };
enum { GX_VA_POS = 9, GX_VA_CLR0 = 11, GX_VA_TEX0 = 13 };
enum { GX_NONE = 0, GX_DIRECT = 1 };
enum { GX_POS_XYZ = 1, GX_CLR_RGBA = 1, GX_TEX_ST = 1 };
enum { GX_F32 = 4, GX_RGBA8 = 5 };
/* matrices */
enum { GX_PERSPECTIVE = 0, GX_ORTHOGRAPHIC = 1 };
enum { GX_PNMTX0 = 0 };
/* state */
enum { GX_NEVER, GX_LESS, GX_EQUAL, GX_LEQUAL, GX_GREATER, GX_NEQUAL, GX_GEQUAL, GX_ALWAYS };
enum { GX_CULL_NONE, GX_CULL_FRONT, GX_CULL_BACK, GX_CULL_ALL };
enum { GX_COLOR0A0 = 4, GX_COLORNULL = 0xff };
enum { GX_SRC_REG = 0, GX_SRC_VTX = 1 };
enum { GX_LIGHTNULL = 0 };
enum { GX_DF_NONE = 0 };
enum { GX_AF_NONE = 2 };
enum { GX_TEVSTAGE0 = 0 };
enum { GX_MODULATE = 0, GX_DECAL, GX_BLEND, GX_REPLACE, GX_PASSCLR };
enum { GX_TEXCOORD0 = 0, GX_TEXCOORDNULL = 0xff };
enum { GX_TEXMAP0 = 0, GX_TEXMAP_NULL = 0xff };
enum { GX_TG_MTX2x4 = 1, GX_TG_TEX0 = 4 };
enum { GX_IDENTITY = 60 };
enum { GX_BM_NONE, GX_BM_BLEND, GX_BM_LOGIC, GX_BM_SUBTRACT };
enum { GX_BL_ZERO, GX_BL_ONE, GX_BL_SRCCLR, GX_BL_INVSRCCLR, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA };
enum { GX_LO_CLEAR = 0 };
enum { GX_PF_RGB8_Z24 = 0, GX_PF_RGB565_Z16 = 2 };
enum { GX_ZC_LINEAR = 0 };
enum { GX_GM_1_0 = 0 };
enum { GX_TF_I4 = 0, GX_TF_I8 = 1, GX_TF_IA8 = 3, GX_TF_RGBA8 = 6 };
enum { GX_CLAMP = 0, GX_REPEAT = 1 };
enum { GX_NEAR = 0, GX_LINEAR = 1 };

void GX_Init(void *fifo, u32 size);
void GX_SetCopyClear(GXColor c, u32 z);
void GX_SetViewport(f32 x, f32 y, f32 w, f32 h, f32 n, f32 f);
f32  GX_GetYScaleFactor(u16 efbh, u16 xfbh);
u32  GX_SetDispCopyYScale(f32 s);
void GX_SetScissor(u32 x, u32 y, u32 w, u32 h);
void GX_SetDispCopySrc(u16 l, u16 t, u16 w, u16 h);
void GX_SetDispCopyDst(u16 w, u16 h);
void GX_SetCopyFilter(u8 aa, u8 pattern[12][2], u8 vf, u8 vfilter[7]);
void GX_SetFieldMode(u8 field, u8 half);
void GX_SetPixelFmt(u8 pix, u8 z);
void GX_SetCullMode(u8 mode);
void GX_SetDispCopyGamma(u8 g);
void GX_ClearVtxDesc(void);
void GX_SetVtxDesc(u8 attr, u8 type);
void GX_SetVtxAttrFmt(u8 fmt, u32 attr, u32 comp, u32 type, u32 frac);
void GX_SetNumChans(u8 n);
void GX_SetChanCtrl(s32 ch, u8 en, u8 amb, u8 mat, u8 lights, u8 diff, u8 attn);
void GX_SetNumTevStages(u8 n);
void GX_SetNumTexGens(u32 n);
void GX_SetTexCoordGen(u16 tc, u32 type, u32 src, u32 mtx);
void GX_SetBlendMode(u8 type, u8 src, u8 dst, u8 op);
void GX_SetAlphaUpdate(u8 en);
void GX_SetColorUpdate(u8 en);
void GX_SetZMode(u8 en, u8 func, u8 upd);
void GX_InvVtxCache(void);
void GX_InvalidateTexAll(void);
void GX_CopyDisp(void *dst, u8 clear);
void GX_DrawDone(void);
void GX_LoadProjectionMtx(Mtx44 m, u8 type);
void GX_LoadPosMtxImm(Mtx m, u32 idx);
void GX_LoadTexObj(GXTexObj *obj, u8 map);
void GX_SetTevOp(u8 stage, u8 mode);
void GX_SetTevOrder(u8 stage, u8 coord, u32 map, u8 color);
void GX_InitTexObj(GXTexObj *obj, void *img, u16 w, u16 h, u8 fmt, u8 ws, u8 wt, u8 mip);
void GX_InitTexObjFilterMode(GXTexObj *obj, u8 minf, u8 magf);
void GX_Begin(u8 prim, u8 fmt, u16 n);
void GX_End(void);
void GX_Position3f32(f32 x, f32 y, f32 z);
void GX_Color4u8(u8 r, u8 g, u8 b, u8 a);
void GX_TexCoord2f32(f32 s, f32 t);

/* matrices */
void guMtxIdentity(Mtx m);
void guMtxCopy(Mtx src, Mtx dst);
void guMtxConcat(Mtx a, Mtx b, Mtx ab);
void guMtxRotAxisDeg(Mtx m, guVector *axis, f32 deg);
void guMtxTransApply(Mtx src, Mtx dst, f32 x, f32 y, f32 z);
void guVecMultiply(Mtx m, guVector *src, guVector *dst);
void guVecMultiplySR(Mtx m, guVector *src, guVector *dst);
void guOrtho(Mtx44 m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f);
void guPerspective(Mtx44 m, f32 fovy, f32 aspect, f32 n, f32 f);

/* video */
#define VI_NON_INTERLACE 1
#define COLOR_BLACK 0x00800080
#define MEM_K0_TO_K1(p) ((void *)(p))
void VIDEO_Init(void);
GXRModeObj *VIDEO_GetPreferredMode(GXRModeObj *mode);
void VIDEO_ClearFrameBuffer(GXRModeObj *m, void *fb, u32 color);
void VIDEO_Configure(GXRModeObj *m);
void VIDEO_SetNextFramebuffer(void *fb);
void VIDEO_SetBlack(bool black);
void VIDEO_Flush(void);
void VIDEO_WaitVSync(void);
void *SYS_AllocateFramebuffer(GXRModeObj *m);
void DCFlushRange(void *p, u32 len);

/* the IPL font */
typedef struct {
    u16 font_type, first_char, last_char, inval_char, asc, desc, width, leading;
    u16 cell_width, cell_height;
    u32 sheet_size;
    u16 sheet_format, sheet_column, sheet_row, sheet_width, sheet_height, width_table;
    u32 sheet_image, sheet_fullsize;
    u8 c0, c1, c2, c3;
} sys_fontheader;
#define SYS_FONTENC_ANSI 0
#define SYS_FONTENC_SJIS 1
#define SYS_FONTSIZE_ANSI (512 * 1024)
#define SYS_FONTSIZE_SJIS (512 * 1024)
u32  SYS_GetFontEncoding(void);
u32  SYS_InitFont(sys_fontheader *hdr);
s32  SYS_GetFontWidth(s32 c);
void SYS_GetFontTexture(s32 c, void **image, s32 *xpos, s32 *ypos, s32 *width);

#define SYS_HOTRESET 1
#define SYS_SHUTDOWN 2
void SYS_ResetSystem(s32 reset, u32 type, s32 force);
