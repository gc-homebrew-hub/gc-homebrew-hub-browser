/* video.h - GX setup and 2D drawing in a fixed 640x480 coordinate space. */
#pragma once
#include <stdbool.h>
#include <gccore.h>

#define SCREEN_W 640
#define SCREEN_H 480

typedef struct { u8 r, g, b, a; } Color;
#define RGBA(r, g, b, a) ((Color){ (r), (g), (b), (a) })
#define RGB(r, g, b)     ((Color){ (r), (g), (b), 255 })

void video_init(void);
void video_frame_begin(void);        /* clears, sets up 2D drawing */
void video_frame_end(void);          /* copies to the framebuffer and waits for vsync */
bool video_is_widescreen(void);
int  video_frame_count(void);

/* 2D (ortho, y down, 640x480 logical pixels) */
void gfx_rect(float x, float y, float w, float h, Color c);
void gfx_rect_gradient(float x, float y, float w, float h, Color top, Color bottom);
void gfx_rect_hgradient(float x, float y, float w, float h, Color left, Color right);
/* Triangle strip from n (x, y) pairs, each vertex with its own colour */
void gfx_strip(const float *xy, const Color *c, int n);
void gfx_round_rect(float x, float y, float w, float h, float radius, Color c);
void gfx_round_rect_gradient(float x, float y, float w, float h, float radius, Color top, Color bottom);
void gfx_circle(float cx, float cy, float r, Color c);
/* Convex polygon from n (x, y) pairs */
void gfx_poly(const float *xy, int n, Color c);
void gfx_frame(float x, float y, float w, float h, float thickness, Color c);
void gfx_line(float x0, float y0, float x1, float y1, float width, Color c);
/* Textured quad from an I4/I8/IA8/RGBA8 texture object; s/t in texels */
void gfx_tex_quad(GXTexObj *tex, float texw, float texh, float x, float y, float w, float h,
                  float s0, float t0, float s1, float t1, Color tint);
