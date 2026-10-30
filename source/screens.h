/* screens.h - every screen of the program and the switch between them. */
#pragma once
#include <stdbool.h>
#include <gctypes.h>

typedef enum { SCR_STORE, SCR_INSTALLED, SCR_SOURCES, SCR_SETTINGS, SCR_DETAILS, SCR_COUNT } Screen;
#define SCR_DOCK (-2)                    /* screens_update: move to the bar of sections */

/* the frame around every screen (menu.c): the title bar along the top and the
 * bar of sections along the bottom, which B moves into and out of */
void menu_focus(void);
bool menu_focused(void);
int  menu_update(u32 pressed);          /* a Screen to open, or -1 */
void menu_draw(bool with_dock);

/* the sections (screens.c) */
void   screens_open(Screen s);
Screen screens_section(void);           /* the section on screen (the details page's parent) */
bool   screens_modal(void);             /* a keyboard or a question is up */
void   screens_ask_exit(void);
int    screens_update(u32 pressed);     /* a Screen to switch to, SCR_DOCK, or -1 to stay */
void   screens_draw(bool focus);        /* focus: draw the selection ring */
void   screens_draw_overlays(void);
