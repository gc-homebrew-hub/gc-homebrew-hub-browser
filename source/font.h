/* font.h - text in Inter, baked at the exact pixel sizes below and drawn 1:1. */
#pragma once
#include <stdbool.h>
#include "video.h"

/* Text sizes used across the program (tools/font/bake_font.py has the pixel sizes) */
typedef enum {
    FS_KEY,                              /* the letter on a button hint */
    FS_SMALL,
    FS_BODY,
    FS_BIG,
    FS_COUNT
} FontSize;

void  font_init(void);
float font_height(FontSize size);                 /* line height in pixels */
float font_width(const char *text, FontSize size);
float font_width_n(const char *text, int n, FontSize size);
void  font_draw(float x, float y, FontSize size, Color c, const char *text);
void  font_draw_center(float cx, float y, FontSize size, Color c, const char *text);
void  font_draw_right(float rx, float y, FontSize size, Color c, const char *text);
/* Draws at most `maxw` pixels wide, ending with "..." when cut. */
void  font_draw_clip(float x, float y, FontSize size, Color c, const char *text, float maxw);
/* Word-wraps into lines font_line_pitch() apart; returns the number of lines
 * drawn (or that would be drawn when draw == false). */
int   font_draw_wrapped(float x, float y, FontSize size, Color c, const char *text, float maxw,
                        int max_lines, bool draw);
float font_line_pitch(FontSize size);
