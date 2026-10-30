/* font.h - text drawn with the GameCube's own IPL font (the one the boot menu uses). */
#pragma once
#include <stdbool.h>
#include "video.h"

bool  font_init(void);
float font_height(float scale);                   /* line height in pixels */
float font_width(const char *text, float scale);
float font_width_n(const char *text, int n, float scale);
void  font_draw(float x, float y, float scale, Color c, const char *text);
void  font_draw_center(float cx, float y, float scale, Color c, const char *text);
void  font_draw_right(float rx, float y, float scale, Color c, const char *text);
/* Draws at most `maxw` pixels wide, ending with "..." when cut. */
void  font_draw_clip(float x, float y, float scale, Color c, const char *text, float maxw);
/* Word-wraps into lines; returns the number of lines drawn (or that would be
 * drawn when draw == false). */
int   font_draw_wrapped(float x, float y, float scale, Color c, const char *text, float maxw,
                        int max_lines, bool draw);

/* Text sizes used across the program */
#define FS_TITLE 1.0f
#define FS_BIG   0.8f
#define FS_BODY  0.62f
#define FS_SMALL 0.5f
