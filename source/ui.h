/* ui.h - the look of the program (GameCube menu colours) and shared widgets. */
#pragma once
#include <stdbool.h>
#include "video.h"
#include "font.h"
#include "input.h"

/* palette: the GameCube main menu's indigo, violet highlights, white text */
#define C_BG_TOP      RGB(56, 42, 128)
#define C_BG_BOTTOM   RGB(16, 10, 46)
#define C_CUBE        RGB(156, 140, 226)
#define C_PANEL       RGBA(255, 255, 255, 28)
#define C_PANEL_EDGE  RGBA(255, 255, 255, 70)
#define C_TILE        RGBA(255, 255, 255, 40)
#define C_TILE_HOT    RGBA(220, 210, 255, 150)
#define C_ROW_HOT     RGBA(255, 255, 255, 60)
#define C_TEXT        RGB(255, 255, 255)
#define C_TEXT_DIM    RGBA(255, 255, 255, 170)
#define C_TEXT_FAINT  RGBA(255, 255, 255, 110)
#define C_ACCENT      RGB(255, 214, 90)     /* selection / update */
#define C_GOOD        RGB(140, 235, 150)
#define C_BAD         RGB(255, 130, 120)
#define C_BAR         RGB(120, 220, 255)

/* safe area */
#define UI_LEFT   32
#define UI_RIGHT  608
#define UI_TOP    24
#define UI_BOTTOM 456

typedef struct { int sel, top, count, rows; } ListState;

void ui_background(void);
void ui_header(const char *title, const char *right);     /* bar with the small cube */
void ui_footer(const char *hints);                         /* "A Open  B Back" */
void ui_panel(float x, float y, float w, float h);
void ui_tag(float rx, float y, const char *text, Color c);  /* small right-aligned label */
void ui_progress(float x, float y, float w, float h, float frac);  /* frac < 0: busy stripe */
void ui_busy_banner(void);                                 /* worker status under the header */
void ui_list_nav(ListState *l, u32 pressed);
void ui_list_clamp(ListState *l);
void ui_row_highlight(float x, float y, float w, float h, bool hot);
void ui_badge(float x, float y, int count);                /* little yellow count bubble */

/* toasts */
void ui_toast(const char *text, bool error);
void ui_toasts_update_draw(void);

/* modal confirm: ui_confirm_open then poll each frame */
void ui_confirm_open(const char *title, const char *text, const char *yes);
bool ui_confirm_active(void);
int  ui_confirm_update(u32 pressed);   /* 1 yes, -1 no, 0 waiting */
void ui_confirm_draw(void);

void ui_format_size(unsigned long long bytes, char *out, size_t cap);
