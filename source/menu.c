/* menu.c - the main menu: the glass cube of the GameCube's own menu, with a
 * section on four of its faces and a tile for each around it. The D-pad (or
 * a push of the Control Stick) turns the cube toward a section, the stick
 * leans it live while held, the C-stick spins it freely and lets it spring
 * back, and opening a section flies into that face. */
#include "screens.h"
#include "ui.h"
#include "catalog.h"
#include "store.h"
#include "net.h"
#include <math.h>
#include <stdio.h>

enum { D_NONE = -1, D_UP, D_DOWN, D_LEFT, D_RIGHT };

static const struct { const char *label; Screen screen; float x, y; int face, icon; Color accent; } tiles[4] = {
    { "SOURCES",   SCR_SOURCES,   320, 84,  CUBE_TOP,    ICON_SOURCES,   C_SOURCES },
    { "INSTALLED", SCR_INSTALLED, 320, 404, CUBE_BOTTOM, ICON_INSTALLED, C_INSTALLED },
    { "SETTINGS",  SCR_SETTINGS,  120, 236, CUBE_LEFT,   ICON_SETTINGS,  C_SETTINGS },
    { "HOMEBREW HUB", SCR_STORE,     520, 236, CUBE_RIGHT,  ICON_STORE,     C_STORE },
};
/* the turn that shows a section's face three-quarters on, and square on */
static const float face_rx[4] = { 62, -62, 14, 14 }, face_ry[4] = { -20, -20, 62, -62 };
static const float open_rx[4] = { 90, -90, 0, 0 },   open_ry[4] = { 0, 0, 90, -90 };

#define OPEN_FRAMES  18
#define INTRO_FRAMES 16
#define CUBE_SIZE    58

static int hover = D_RIGHT;
static float rx = 14, ry = -62, orbit_x, orbit_y;
static int anim;                 /* frames left of flying into a face, 0 = idle */
static int anim_dir;
static int intro;                /* frames left of coming back out of one */
static float hot_amount[4];

void menu_reset(void) { anim = 0; intro = INTRO_FRAMES; }

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
        if (dir == hover) { anim = OPEN_FRAMES; anim_dir = dir; }
        else hover = dir;
    }
    if ((p & BTN_A) && hover != D_NONE) { anim = OPEN_FRAMES; anim_dir = hover; }
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

static float fit(const char *t, float maxw, float scale) {
    float w = font_width(t, 1.0f);
    return w * scale > maxw ? maxw / w : scale;
}

static float smooth(float t) { return t * t * (3 - 2 * t); }

/* What each face of the big cube shows: a section's pictogram and name, or
 * the program's name on the front and back. */
static void face_draw(int face, float f, void *user) {
    (void)user;
    for (int i = 0; i < 4; i++) {
        if (tiles[i].face != face) continue;
        float hot = hot_amount[i];
        gfx_rect(2, 2, 96, 96, ui_alpha(tiles[i].accent, (0.06f + 0.16f * hot) * f));
        gfx_circle(50, 40, 30, ui_alpha(tiles[i].accent, (0.12f + 0.16f * hot) * f));
        ui_icon(tiles[i].icon, 50, 40, 42, ui_alpha(ui_mix(tiles[i].accent, RGB(255, 255, 255), 0.3f), f));
        font_draw_center(50, 70, fit(tiles[i].label, 86, 0.62f), ui_alpha(C_TEXT, f), tiles[i].label);
        return;
    }
    font_draw_center(50, 8, fit("GC", 70, 1.4f), ui_alpha(C_TEXT, f), "GC");
    gfx_rect(18, 50, 64, 2, ui_alpha(C_CUBE_EDGE, f));
    float sc = fit("HOMEBREW", 84, 0.6f);
    font_draw_center(50, 56, sc, ui_alpha(C_TEXT_DIM, f), "HOMEBREW");
    font_draw_center(50, 58 + font_height(sc), sc, ui_alpha(C_TEXT_DIM, f), "BROWSER");
}

void menu_draw(void) {
    int f = video_frame_count();
    Color accent = hover >= 0 ? tiles[hover].accent : C_STORE;
    ui_set_accent(accent);
    ui_menu_background();

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
    ui_led(UI_RIGHT - font_width(status, FS_SMALL) - 12, UI_TOP + 8 + font_height(FS_SMALL) / 2,
           store_busy() ? C_ACCENT : store_net_up() ? C_GOOD : C_BAD, store_busy());
    if (store_busy()) ui_progress(UI_RIGHT - 200, UI_TOP + 30, 200, 8, store_progress());

    /* the cube: turned toward the hovered face, leaned by the stick, spun by
     * the C-stick, with a slow sway and bob of its own */
    float sx, sy, cx, cy;
    input_sticks(&sx, &sy, &cx, &cy);
    if (cx != 0 || cy != 0) {
        orbit_y += cx * 4.5f;
        orbit_x += cy * 4.5f;
        if (orbit_x > 75) orbit_x = 75;
        if (orbit_x < -75) orbit_x = -75;
    } else {
        orbit_y = remainderf(orbit_y, 360.0f) * 0.88f;
        orbit_x *= 0.88f;
    }
    int h = hover >= 0 ? hover : D_RIGHT;
    float trx = face_rx[h] + sy * 16 + 3 * sinf(f * 0.017f);
    float try_ = face_ry[h] - sx * 16 + 5 * sinf(f * 0.021f);
    rx += (trx - rx) * 0.12f;
    ry += (try_ - ry) * 0.12f;
    float crx = rx + orbit_x, cry = ry + orbit_y, size = CUBE_SIZE, alpha = 1, tiles_a = 1;
    if (anim || intro) {
        /* e: 0 resting .. 1 square on the face, filling the screen */
        float e = anim ? smooth(1.0f - (float)anim / OPEN_FRAMES) : smooth((float)intro / INTRO_FRAMES);
        int d = anim ? anim_dir : h;
        crx += (open_rx[d] - crx) * e;
        cry += (open_ry[d] - cry) * e;
        size *= 1 + 1.7f * e;
        alpha = 1 - e * e;
        tiles_a = 1 - e;
        if (intro) intro--;
    }
    float bob = 6.0f * sinf(f * 0.035f);
    float ccx = 320, ccy = 236 + bob;
    /* a soft shadow and a neon pool of light under the cube */
    float fy = 236 + CUBE_SIZE * 1.85f;
    ui_soft_ellipse(320, fy, 110, 16, ui_alpha(accent, 0.35f * alpha));
    ui_soft_ellipse(320, fy, 70 - bob, 9, RGBA(0, 0, 0, (u8)(110 * alpha)));
    CubeStyle st = { C_CUBE, RGBA(C_CUBE_CORE.r, C_CUBE_CORE.g, C_CUBE_CORE.b, 230),
                     ui_mix(C_CUBE_EDGE, accent, 0.3f), alpha, f * 1.2f, face_draw, NULL };
    gfx_glass_cube(ccx, ccy, size, crx, cry, 0, &st);

    /* the four tiles */
    for (int i = 0; i < 4; i++) {
        bool hot = hover == i;
        hot_amount[i] += ((hot ? 1.0f : 0.0f) - hot_amount[i]) * 0.2f;
        float k = hot_amount[i];
        if (tiles_a <= 0.01f) continue;
        float grow = k * 8;
        float w = 176 + grow, ht = 56 + grow * 0.5f;
        float x = tiles[i].x - w / 2, y = tiles[i].y - ht / 2;
        Color ac = tiles[i].accent;
        if (k > 0.02f) gfx_round_rect(x - 4, y - 4, w + 8, ht + 8, 16, ui_alpha(ac, 0.3f * k * tiles_a));
        ui_glass(x, y, w, ht, 12, ui_mix(RGBA(255, 255, 255, 30), RGBA(ac.r, ac.g, ac.b, 150), k), tiles_a);
        ui_icon(tiles[i].icon, x + 25, y + ht / 2, 24,
                ui_alpha(ui_mix(ac, RGB(255, 255, 255), 0.25f + 0.5f * k), tiles_a));
        font_draw_clip(x + 48, y + 8, fit(tiles[i].label, w - 56, FS_BODY), ui_alpha(C_TEXT, tiles_a), tiles[i].label, w - 56);
        char sub[64];
        tile_text(i, sub, sizeof sub);
        font_draw_clip(x + 48, y + 8 + font_height(FS_BODY), FS_SMALL, ui_alpha(C_TEXT_DIM, tiles_a), sub, w - 56);
        if (i == D_DOWN) ui_badge(x + w - 26, y - 8, catalog_update_count());
    }

    ui_footer("D-pad  Choose    A  Open    C  Spin the cube    X  Reload the catalogs");
}
