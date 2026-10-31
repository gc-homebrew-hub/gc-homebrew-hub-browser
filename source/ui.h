/* ui.h - the look of the program, after the Swiss "System" theme: a dark
 * TV-screen backdrop, teal-black framed panels, frosted list rows, grey glass
 * buttons, an indigo description bar and a gold ring around the selection.
 * Every screen shares one frame: the title bar along the top, the bar of
 * sections along the bottom, a side pane on the left and a list on the right. */
#pragma once
#include <stdbool.h>
#include "video.h"
#include "font.h"
#include "input.h"

/* text */
#define C_TEXT        RGB(255, 255, 255)
#define C_TEXT_DIM    RGB(216, 224, 236)
#define C_TEXT_FAINT  RGB(176, 188, 204)
#define C_TEXT_DARK   RGB(30, 23, 0)       /* on gold */
/* state */
#define C_GOLD        RGB(246, 218, 84)    /* the selection, updates */
#define C_GOOD        RGB(150, 236, 168)
#define C_BAD         RGB(255, 150, 136)

/* the frame, in the 640x480 layout */
#define FRAME_X     19
#define FRAME_W     602
#define TITLE_Y     17
#define TITLE_H     62
#define DOCK_Y      431
#define DOCK_H      42
#define BODY_Y      94                      /* between the title bar and the dock */
#define BODY_BOTTOM 416
#define SIDE_X      FRAME_X
#define SIDE_W      166
#define MAIN_X      197                     /* the list or panel right of the side pane */
#define MAIN_W      (FRAME_X + FRAME_W - MAIN_X)

/* the list: frosted rows, then the description bar under them */
#define ROW_H       46
#define ROW_PITCH   52
#define LIST_ROWS   5
#define ROW_W       (MAIN_W - 16)           /* room for the scroll bar */
#define DESC_Y      (BODY_Y + LIST_ROWS * ROW_PITCH + 2)
#define DESC_H      (BODY_BOTTOM - DESC_Y)

typedef struct { int sel, top, count, rows; } ListState;

Color ui_mix(Color a, Color b, float t);          /* a toward b by t */
Color ui_alpha(Color c, float a);                 /* c with its alpha scaled by a */

/* the pieces of the look */
void ui_backdrop(void);                                         /* the whole screen */
void ui_outline(float x, float y, float w, float h, float r, float t, Color c);
void ui_panel(float x, float y, float w, float h);               /* framed teal-black box */
void ui_row(float x, float y, float w, float h);                /* frosted list row */
void ui_button(float x, float y, float w, float h, bool gold);   /* grey (or gold) glass */
void ui_ring(float x, float y, float w, float h, bool glow);     /* gold selection, just outside the box */
void ui_desc_bar(float x, float y, float w, float h);           /* indigo strip */
void ui_track(float x, float y, float w, float h);              /* dark groove */
void ui_gold_fill(float x, float y, float w, float h);          /* what fills a groove */
void ui_progress(float x, float y, float w, float h, float frac);  /* frac < 0: busy stripe */
void ui_scrollbar(float x, float y, float h, const ListState *l);

/* the frame */
void ui_title_bar(void);
/* A side pane with a heading; returns the y where its contents start. */
float ui_side_pane(const char *heading);
/* Button hints at the bottom of the side pane, one per line: entries split by
 * four spaces, each "KEY  label" (A, B, X, Y, Z, L, R, Start, D-pad). */
void ui_side_hints(const char *hints);
/* A line of the side pane: a dim label and its value, moving *y down. */
void ui_side_line(float *y, const char *label, const char *value, Color vc);
/* The description bar under the list, with up to two lines of text. */
void ui_desc(const char *line1, const char *line2);
/* Draws one hint, "KEY  label", at (x, y); returns its width. */
float ui_hint(float x, float y, const char *hint);
/* Hints in a row; entries split by four spaces. */
void ui_hints(float x, float y, const char *hints);

void ui_list_nav(ListState *l, u32 pressed);
void ui_list_clamp(ListState *l);
/* A row of the list on the right: (x, y) of row r, frosted, ringed when selected */
void ui_list_row(int r, bool selected, bool focus, float *x, float *y);
/* Right-aligned status text on a row */
void ui_row_status(float y, const char *text, Color c);
/* A small plate with an app's initials, like a banner in a file list */
void ui_plate(float x, float y, float w, float h, const char *name);
void ui_initials(const char *name, char out[3]);

/* Pictograms for the sections, centred on (cx, cy), about s pixels across */
enum { ICON_STORE, ICON_INSTALLED, ICON_SOURCES, ICON_SETTINGS, ICON_EXIT };
void ui_icon(int icon, float cx, float cy, float s, Color c);
void ui_count_badge(float rx, float y, int count);              /* gold bubble, right edge at rx */

/* toasts */
void ui_toast(const char *text, bool error);
void ui_toasts_update_draw(void);

/* modal confirm: ui_confirm_open then poll each frame; yes is on A, no on B */
void ui_confirm_open(const char *title, const char *text, const char *yes, const char *no);
bool ui_confirm_active(void);
int  ui_confirm_update(u32 pressed);   /* 1 yes, -1 no, 0 waiting */
void ui_confirm_draw(void);

void ui_format_size(unsigned long long bytes, char *out, size_t cap);
