/* screens.h - every screen of the program and the switch between them. */
#pragma once
#include <gctypes.h>

typedef enum { SCR_MENU, SCR_SOURCES, SCR_INSTALLED, SCR_STORE, SCR_SETTINGS, SCR_DETAILS, SCR_COUNT } Screen;

/* main menu (menu.c) */
void menu_reset(void);
int  menu_update(u32 pressed);          /* a Screen to open, or -1 */
void menu_draw(void);

/* the sections (screens.c) */
void screens_open(Screen s);
int  screens_update(u32 pressed);       /* SCR_MENU to go back, another Screen to switch, -1 to stay */
void screens_draw(void);
