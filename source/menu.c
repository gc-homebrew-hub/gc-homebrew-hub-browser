/* menu.c - the frame every screen sits in: the title bar along the top and,
 * along the bottom, a panel of five glass buttons, one per section. The
 * section on screen is the gold button; B moves into the bar, where the
 * D-pad picks a button (the gold ring marks it) and A opens it. */
#include "screens.h"
#include "ui.h"
#include "catalog.h"
#include "store.h"
#include <stdio.h>

#define NBUTTONS 5
#define EXIT_BUTTON 4
static const struct { const char *label; int screen, icon; } buttons[NBUTTONS] = {
    { "Hub",       SCR_STORE,     ICON_STORE },
    { "Installed", SCR_INSTALLED, ICON_INSTALLED },
    { "Sources",   SCR_SOURCES,   ICON_SOURCES },
    { "Settings",  SCR_SETTINGS,  ICON_SETTINGS },
    { "Exit",      -1,            ICON_EXIT },
};

static bool focused;
static int sel;

void menu_focus(void) {
    focused = true;
    Screen s = screens_section();
    for (int i = 0; i < NBUTTONS; i++)
        if (buttons[i].screen == (int)s) sel = i;
}

bool menu_focused(void) { return focused; }

int menu_update(u32 p) {
    if (!focused) return -1;
    if (p & BTN_LEFT) sel = (sel + NBUTTONS - 1) % NBUTTONS;
    if (p & BTN_RIGHT) sel = (sel + 1) % NBUTTONS;
    if (p & (BTN_B | BTN_UP)) focused = false;
    if (p & BTN_X) store_request_refresh();
    if (p & BTN_A) {
        focused = false;
        if (sel == EXIT_BUTTON) { screens_ask_exit(); return -1; }
        return buttons[sel].screen;
    }
    return -1;
}

void menu_draw(void) {
    ui_title_bar();
    const float bw = 108, bh = 30, gap = 10, by = DOCK_Y + 6;
    const float x0 = FRAME_X + (FRAME_W - NBUTTONS * bw - (NBUTTONS - 1) * gap) / 2;
    ui_panel(FRAME_X, DOCK_Y, FRAME_W, DOCK_H);
    Screen cur = screens_section();
    for (int i = 0; i < NBUTTONS; i++) {
        float bx = x0 + i * (bw + gap);
        bool here = buttons[i].screen == (int)cur;
        ui_button(bx, by, bw, bh, here);
        if (focused && sel == i) ui_ring(bx, by, bw, bh, true);
        Color c = here ? C_TEXT_DARK : C_TEXT;
        /* the pictogram and the label, centred together */
        float lw = font_width(buttons[i].label, FS_SMALL), gx = bx + (bw - 16 - 7 - lw) / 2;
        ui_icon(buttons[i].icon, gx + 8, by + bh / 2, 16, c);
        font_draw(gx + 23, by + (bh - font_height(FS_SMALL)) / 2, FS_SMALL, c, buttons[i].label);
        if (buttons[i].screen == SCR_INSTALLED) ui_count_badge(bx + bw + 4, by - 8, catalog_update_count());
    }
}
