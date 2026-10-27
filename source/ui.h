/* ui.h - the look of the program (the GameCube menu: indigo glass, a dark
 * grid behind the sections, neon accents) and shared widgets. */
#pragma once
#include <stdbool.h>
#include "video.h"
#include "font.h"
#include "input.h"

/* palette: the main menu's indigo, the glass cube, white text */
#define C_BG_TOP      RGB(44, 30, 112)
#define C_BG_BOTTOM   RGB(6, 4, 26)
#define C_GRID_TOP    RGB(14, 12, 34)      /* the sections' dark grid */
#define C_GRID_BOTTOM RGB(3, 3, 10)
#define C_CUBE        RGB(124, 104, 236)
#define C_CUBE_CORE   RGB(186, 170, 255)
#define C_CUBE_EDGE   RGB(206, 196, 255)
#define C_PANEL       RGBA(255, 255, 255, 22)
#define C_PANEL_EDGE  RGBA(255, 255, 255, 70)
#define C_TILE        RGBA(255, 255, 255, 34)
#define C_ROW_HOT     RGBA(255, 255, 255, 60)
#define C_TEXT        RGB(255, 255, 255)
#define C_TEXT_DIM    RGBA(255, 255, 255, 170)
#define C_TEXT_FAINT  RGBA(255, 255, 255, 110)
#define C_TEXT_DARK   RGB(24, 16, 56)      /* on a lit highlight */
#define C_ACCENT      RGB(255, 214, 90)    /* update */
#define C_GOOD        RGB(110, 240, 150)
#define C_BAD         RGB(255, 110, 110)
#define C_BAR         RGB(110, 220, 255)

/* one neon accent per section, Y2K style */
#define C_SOURCES     RGB(70, 190, 255)    /* blue */
#define C_INSTALLED   RGB(90, 240, 140)    /* green */
#define C_STORE       RGB(196, 120, 255)   /* violet */
#define C_SETTINGS    RGB(255, 160, 56)    /* GameCube orange */

/* safe area */
#define UI_LEFT   32
#define UI_RIGHT  608
#define UI_TOP    24
#define UI_BOTTOM 456

typedef struct { int sel, top, count, rows; } ListState;

/* The accent of the section on screen; widgets take their highlight from it. */
void  ui_set_accent(Color c);
Color ui_accent(void);
Color ui_mix(Color a, Color b, float t);          /* a toward b by t */
Color ui_alpha(Color c, float a);                 /* c with its alpha scaled by a */

void ui_background(void);                                  /* a section: the dark grid */
void ui_menu_background(void);                             /* the main menu: indigo, a floor grid */
void ui_header(const char *title, const char *right);     /* bar with the small cube */
void ui_footer(const char *hints);                         /* "A  Open    B  Back" */
/* Draws button hints: entries split by four spaces, each "KEY  label"; A, B,
 * X, Y, Z, L, R, Start and D-pad become the controller's buttons. */
void ui_hints(float x, float y, const char *hints, float alpha);
/* Translucent glossy panel: tint over glass, a gloss on the top half, a rim. */
void ui_glass(float x, float y, float w, float h, float r, Color tint, float alpha);
void ui_panel(float x, float y, float w, float h);
void ui_tag(float rx, float y, const char *text, Color c);  /* small right-aligned label */
void ui_progress(float x, float y, float w, float h, float frac);  /* frac < 0: busy stripe */
void ui_busy_banner(void);                                 /* worker status under the header */
void ui_list_nav(ListState *l, u32 pressed);
void ui_list_clamp(ListState *l);
void ui_row_highlight(float x, float y, float w, float h, bool hot);
void ui_badge(float x, float y, int count);                /* little yellow count bubble */
void ui_led(float cx, float cy, Color c, bool pulse);      /* a glowing status pixel */
/* A soft-edged ellipse: a pool of light or a shadow (c.a at the middle) */
void ui_soft_ellipse(float cx, float cy, float rx, float ry, Color c);

/* Pictograms for the sections, centred on (cx, cy), about s pixels across */
enum { ICON_SOURCES, ICON_INSTALLED, ICON_STORE, ICON_SETTINGS };
void ui_icon(int icon, float cx, float cy, float s, Color c);

/* Apps get a colour and a two-letter block, like a save file's banner. */
Color ui_app_color(const char *key);
void  ui_initials(const char *name, char out[3]);
void  ui_app_block(float x, float y, float s, const char *name, const char *key, float alpha);

/* toasts */
void ui_toast(const char *text, bool error);
void ui_toasts_update_draw(void);

/* modal confirm: ui_confirm_open then poll each frame */
void ui_confirm_open(const char *title, const char *text, const char *yes);
bool ui_confirm_active(void);
int  ui_confirm_update(u32 pressed);   /* 1 yes, -1 no, 0 waiting */
void ui_confirm_draw(void);

void ui_format_size(unsigned long long bytes, char *out, size_t cap);
