/* gx_soft.h - the preview's frame and switches. */
#pragma once

#define SOFT_W 640
#define SOFT_H 480

extern float soft_fb[SOFT_H][SOFT_W][3];
extern float soft_shown[SOFT_H][SOFT_W][3];   /* the last frame copied out */
extern int soft_enabled;          /* 0: geometry is accepted but not drawn */

int soft_load_font(const char *path);
