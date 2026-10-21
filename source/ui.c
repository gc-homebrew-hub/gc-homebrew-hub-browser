#include "ui.h"
#include "store.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

void ui_background(void) {
    gfx_rect_gradient(0, 0, SCREEN_W, SCREEN_H, C_BG_TOP, C_BG_BOTTOM);
    /* a soft glow behind the middle, like the menu's spotlight */
    float t = video_frame_count() * 0.01f;
    Color glow = RGBA(120, 100, 220, 26 + (u8)(10 * sinf(t)));
    gfx_round_rect(120, 90, 400, 300, 150, glow);
}

void ui_header(const char *title, const char *right) {
    gfx_rect(0, 0, SCREEN_W, 58, RGBA(0, 0, 0, 70));
    gfx_rect(0, 58, SCREEN_W, 2, C_PANEL_EDGE);
    gfx_cube(UI_LEFT + 16, 30, 13, 22, video_frame_count() * 0.9f, 0, C_CUBE, 1.0f);
    font_draw_shadow(UI_LEFT + 44, 14, FS_BIG, C_TEXT, title);
    if (right && *right) font_draw_right(UI_RIGHT, 22, FS_SMALL, C_TEXT_DIM, right);
}

void ui_footer(const char *hints) {
    gfx_rect(0, UI_BOTTOM - 4, SCREEN_W, SCREEN_H - UI_BOTTOM + 4, RGBA(0, 0, 0, 60));
    font_draw_shadow(UI_LEFT, UI_BOTTOM + 2, FS_SMALL, C_TEXT_DIM, hints);
}

void ui_panel(float x, float y, float w, float h) {
    gfx_round_rect(x + 3, y + 4, w, h, 12, RGBA(0, 0, 0, 50));
    gfx_round_rect(x, y, w, h, 12, C_PANEL);
}

void ui_tag(float rx, float y, const char *text, Color c) {
    float w = font_width(text, FS_SMALL) + 14;
    gfx_round_rect(rx - w, y - 2, w, font_height(FS_SMALL) + 4, 6, RGBA(c.r, c.g, c.b, 60));
    font_draw_shadow(rx - w + 7, y, FS_SMALL, c, text);
}

void ui_progress(float x, float y, float w, float h, float frac) {
    gfx_round_rect(x, y, w, h, h / 2, RGBA(0, 0, 0, 90));
    if (frac >= 0) {
        if (frac > 1) frac = 1;
        if (frac * w > h) gfx_round_rect(x, y, w * frac, h, h / 2, C_BAR);
    } else {
        float p = fmodf(video_frame_count() * 3.0f, w + 80) - 80;
        float s = p < 0 ? 0 : p, e = p + 80 > w ? w : p + 80;
        if (e - s > h) gfx_round_rect(x + s, y, e - s, h, h / 2, C_BAR);
    }
}

void ui_busy_banner(void) {
    if (!store_busy()) return;
    const char *step = store_step();
    if (!*step) step = "Working";
    char line[160];
    const char *name = store_job_name();
    if (*name) snprintf(line, sizeof line, "%s: %s", step, name);
    else snprintf(line, sizeof line, "%s", step);
    float y = 66;
    gfx_round_rect(UI_LEFT, y, UI_RIGHT - UI_LEFT, 30, 8, RGBA(0, 0, 0, 80));
    font_draw_clip(UI_LEFT + 12, y + 4, FS_SMALL, C_TEXT, line, 330);
    ui_progress(UI_RIGHT - 220, y + 10, 208, 10, store_progress());
}

void ui_list_clamp(ListState *l) {
    if (l->count <= 0) { l->sel = 0; l->top = 0; return; }
    if (l->sel >= l->count) l->sel = l->count - 1;
    if (l->sel < 0) l->sel = 0;
    if (l->rows < 1) l->rows = 1;
    if (l->sel < l->top) l->top = l->sel;
    if (l->sel >= l->top + l->rows) l->top = l->sel - l->rows + 1;
    if (l->top > l->count - l->rows) l->top = l->count - l->rows;
    if (l->top < 0) l->top = 0;
}

void ui_list_nav(ListState *l, u32 p) {
    if (l->count <= 0) return;
    if (p & BTN_DOWN) l->sel = l->sel + 1 < l->count ? l->sel + 1 : 0;
    if (p & BTN_UP) l->sel = l->sel > 0 ? l->sel - 1 : l->count - 1;
    if (p & BTN_R) l->sel += l->rows;
    if (p & BTN_L) l->sel -= l->rows;
    ui_list_clamp(l);
}

void ui_row_highlight(float x, float y, float w, float h, bool hot) {
    if (!hot) return;
    gfx_round_rect(x, y, w, h, 8, C_ROW_HOT);
    gfx_rect(x + 4, y + 6, 3, h - 12, C_ACCENT);
}

void ui_badge(float x, float y, int count) {
    if (count <= 0) return;
    char s[8];
    snprintf(s, sizeof s, "%d", count > 99 ? 99 : count);
    float w = font_width(s, FS_SMALL) + 12;
    if (w < 22) w = 22;
    gfx_round_rect(x, y, w, 20, 10, C_ACCENT);
    font_draw(x + (w - font_width(s, FS_SMALL)) / 2, y + 1, FS_SMALL, RGB(40, 30, 10), s);
}

/* ---- toasts ---- */

#define TOASTS 3
static struct { char text[160]; bool error; int ttl; } toasts[TOASTS];

void ui_toast(const char *text, bool error) {
    memmove(&toasts[1], &toasts[0], sizeof toasts[0] * (TOASTS - 1));
    snprintf(toasts[0].text, sizeof toasts[0].text, "%s", text);
    toasts[0].error = error;
    toasts[0].ttl = error ? 300 : 200;
}

void ui_toasts_update_draw(void) {
    char m[160]; bool err;
    while (store_take_message(m, sizeof m, &err)) ui_toast(m, err);
    float y = UI_BOTTOM - 40;
    for (int i = 0; i < TOASTS; i++) {
        if (toasts[i].ttl <= 0) continue;
        toasts[i].ttl--;
        float a = toasts[i].ttl < 30 ? toasts[i].ttl / 30.0f : 1.0f;
        float w = font_width(toasts[i].text, FS_SMALL) + 28;
        if (w > UI_RIGHT - UI_LEFT) w = UI_RIGHT - UI_LEFT;
        float x = (SCREEN_W - w) / 2;
        Color bg = toasts[i].error ? RGBA(120, 30, 30, (u8)(220 * a)) : RGBA(30, 30, 80, (u8)(220 * a));
        gfx_round_rect(x, y, w, 28, 10, bg);
        gfx_round_rect(x, y, w, 28, 10, RGBA(255, 255, 255, (u8)(30 * a)));
        font_draw_clip(x + 14, y + 5, FS_SMALL, RGBA(255, 255, 255, (u8)(255 * a)), toasts[i].text, w - 28);
        y -= 34;
    }
}

/* ---- confirm ---- */

static struct { bool active; char title[64], text[200], yes[24]; } cf;

void ui_confirm_open(const char *title, const char *text, const char *yes) {
    cf.active = true;
    snprintf(cf.title, sizeof cf.title, "%s", title);
    snprintf(cf.text, sizeof cf.text, "%s", text);
    snprintf(cf.yes, sizeof cf.yes, "%s", yes);
}

bool ui_confirm_active(void) { return cf.active; }

int ui_confirm_update(u32 p) {
    if (!cf.active) return 0;
    if (p & BTN_A) { cf.active = false; return 1; }
    if (p & BTN_B) { cf.active = false; return -1; }
    return 0;
}

void ui_confirm_draw(void) {
    if (!cf.active) return;
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, 120));
    float w = 420, h = 170, x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2;
    gfx_round_rect(x + 4, y + 6, w, h, 14, RGBA(0, 0, 0, 90));
    gfx_round_rect(x, y, w, h, 14, RGB(52, 40, 110));
    gfx_round_rect(x, y, w, h, 14, RGBA(255, 255, 255, 25));
    font_draw_shadow(x + 24, y + 18, FS_BIG, C_TEXT, cf.title);
    font_draw_wrapped(x + 24, y + 56, FS_BODY, C_TEXT_DIM, cf.text, w - 48, 3, true);
    char hint[80];
    snprintf(hint, sizeof hint, "A  %s      B  Cancel", cf.yes);
    font_draw_shadow(x + 24, y + h - 34, FS_SMALL, C_ACCENT, hint);
}

void ui_format_size(unsigned long long b, char *out, size_t cap) {
    if (b >= (1ull << 20)) snprintf(out, cap, "%.1f MB", b / 1048576.0);
    else if (b >= 1024) snprintf(out, cap, "%llu KB", b / 1024);
    else snprintf(out, cap, "%llu bytes", b);
}
