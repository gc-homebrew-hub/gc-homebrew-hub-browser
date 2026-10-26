/* menu.c - the main menu: a floating cube with the four sections around it,
 * in the spirit of the GameCube's own menu. */
#include "screens.h"
#include "ui.h"
#include "catalog.h"
#include "store.h"
#include "net.h"
#include <math.h>
#include <stdio.h>

enum { D_NONE = -1, D_UP, D_DOWN, D_LEFT, D_RIGHT };

static int hover = D_RIGHT;
static float tilt_x, tilt_y, spin;
static int anim;                 /* frames left of the opening roll, 0 = idle */
static int anim_dir;
static float hot_amount[4];

static const struct { const char *label; Screen screen; float x, y; } tiles[4] = {
    { "SOURCES",   SCR_SOURCES,   320, 92 },
    { "INSTALLED", SCR_INSTALLED, 320, 388 },
    { "SETTINGS",  SCR_SETTINGS,  128, 240 },
    { "APP STORE", SCR_STORE,     512, 240 },
};

void menu_reset(void) { anim = 0; }

int menu_update(u32 p) {
    if (anim) {
        if (--anim == 0) return tiles[anim_dir].screen;
        return -1;
    }
    int dir = D_NONE;
    if (p & BTN_UP) dir = D_UP;
    else if (p & BTN_DOWN) dir = D_DOWN;
    else if (p & BTN_LEFT) dir = D_LEFT;
    else if (p & BTN_RIGHT) dir = D_RIGHT;
    if (dir != D_NONE) {
        if (dir == hover) { anim = 16; anim_dir = dir; }
        else hover = dir;
    }
    if ((p & BTN_A) && hover != D_NONE) { anim = 16; anim_dir = hover; }
    if (p & BTN_X) store_request_refresh();
    return -1;
}

static void tile_text(int i, char *sub, size_t cap) {
    switch (i) {
    case D_UP: {
        int n = catalog_source_count(), on = 0;
        for (int k = 0; k < n; k++) if (catalog_source(k)->enabled) on++;
        snprintf(sub, cap, "%d source%s, %d on", n, n == 1 ? "" : "s", on);
        break;
    }
    case D_DOWN: {
        int n = catalog_installed_count(), up = catalog_update_count();
        if (up) snprintf(sub, cap, "%d app%s, %d update%s", n, n == 1 ? "" : "s", up, up == 1 ? "" : "s");
        else snprintf(sub, cap, "%d app%s on the card", n, n == 1 ? "" : "s");
        break;
    }
    case D_RIGHT: snprintf(sub, cap, "%d app%s available", catalog_app_count(), catalog_app_count() == 1 ? "" : "s"); break;
    default: snprintf(sub, cap, "Network, exit"); break;
    }
}

void menu_draw(void) {
    int f = video_frame_count();
    /* title and status */
    font_draw_shadow(UI_LEFT, UI_TOP, FS_BIG, C_TEXT, "GC HOMEBREW HUB BROWSER");
    char status[160];
    if (store_busy()) {
        const char *name = store_job_name();
        if (*name) snprintf(status, sizeof status, "%s %s", store_step(), name);
        else snprintf(status, sizeof status, "%s", store_step());
    } else if (store_net_up()) snprintf(status, sizeof status, "Online  %s", net_ip_string());
    else snprintf(status, sizeof status, "Offline");
    font_draw_right(UI_RIGHT, UI_TOP + 8, FS_SMALL, C_TEXT_DIM, status);
    if (store_busy()) ui_progress(UI_RIGHT - 200, UI_TOP + 30, 200, 8, store_progress());

    /* cube motion: slow spin, a little tilt toward the hovered side, a bob */
    float tx = 0, ty = 0;
    if (hover == D_UP) tx = -28; else if (hover == D_DOWN) tx = 28;
    else if (hover == D_LEFT) ty = -32; else if (hover == D_RIGHT) ty = 32;
    tilt_x += (tx - tilt_x) * 0.12f;
    tilt_y += (ty - tilt_y) * 0.12f;
    spin += 0.35f;
    float bob = 7.0f * sinf(f * 0.035f);
    float roll_x = 0, roll_y = 0, scale = 1.0f;
    if (anim) {
        float t = 1.0f - anim / 16.0f;
        float e = t * t * (3 - 2 * t);
        if (anim_dir == D_UP) roll_x = -90 * e; else if (anim_dir == D_DOWN) roll_x = 90 * e;
        else if (anim_dir == D_LEFT) roll_y = -90 * e; else roll_y = 90 * e;
        scale = 1.0f + 0.35f * e;
    }
    /* shadow under the cube */
    gfx_round_rect(320 - 60, 240 + 78 - bob * 0.3f, 120, 18, 9, RGBA(0, 0, 0, 70));
    gfx_cube(320, 240 + bob, 62 * scale, 22 + tilt_x + roll_x, spin + tilt_y + roll_y, 0, C_CUBE, 1.0f);

    /* the four tiles */
    for (int i = 0; i < 4; i++) {
        bool hot = hover == i;
        hot_amount[i] += ((hot ? 1.0f : 0.0f) - hot_amount[i]) * 0.2f;
        float grow = hot_amount[i] * 8;
        float w = 176 + grow, h = 64 + grow * 0.5f;
        float x = tiles[i].x - w / 2, y = tiles[i].y - h / 2;
        gfx_round_rect(x + 3, y + 4, w, h, 12, RGBA(0, 0, 0, 60));
        gfx_round_rect(x, y, w, h, 12, C_TILE);
        gfx_round_rect(x, y, w, h, 12, RGBA(220, 210, 255, (u8)(130 * hot_amount[i])));
        if (hot) gfx_frame(x, y, w, h, 2, RGBA(255, 255, 255, 160));
        Color label = hot ? RGB(40, 28, 90) : C_TEXT;
        font_draw_center(tiles[i].x, y + 9, FS_BODY, label, tiles[i].label);
        char sub[64];
        tile_text(i, sub, sizeof sub);
        font_draw_center(tiles[i].x, y + 9 + font_height(FS_BODY), FS_SMALL,
                         hot ? RGBA(40, 28, 90, 200) : C_TEXT_DIM, sub);
        if (i == D_DOWN) ui_badge(x + w - 26, y - 8, catalog_update_count());
    }
    /* arrows hinting the directions */
    Color ar = RGBA(255, 255, 255, 90);
    gfx_line(320, 150, 320, 136, 3, ar); gfx_line(320, 330, 320, 344, 3, ar);
    gfx_line(230, 240, 216, 240, 3, ar); gfx_line(410, 240, 424, 240, 3, ar);

    ui_footer("D-pad  Choose    A  Open    X  Reload the catalogs");
}
