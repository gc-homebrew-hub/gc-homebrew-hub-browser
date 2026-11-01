#include "ui.h"
#include "catalog.h"
#include "log.h"
#include "net.h"
#include "store.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

Color ui_mix(Color a, Color b, float t) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return RGBA((u8)(a.r + (b.r - a.r) * t), (u8)(a.g + (b.g - a.g) * t),
                (u8)(a.b + (b.b - a.b) * t), (u8)(a.a + (b.a - a.a) * t));
}

Color ui_alpha(Color c, float a) {
    if (a < 0) a = 0;
    if (a > 1) a = 1;
    return RGBA(c.r, c.g, c.b, (u8)(c.a * a));
}

static inline float clamp01(float t) { return t < 0 ? 0 : t > 1 ? 1 : t; }

/* ---- the backdrop: a dark picture tube ---- */

/* A grid of cols x rows cells over the box, coloured per vertex by fn(u, v)
 * with u, v from -1 to 1, drawn one strip per row. */
#define MESH_MAX 25
static void mesh(float x, float y, float w, float h, int cols, int rows, Color (*fn)(float u, float v)) {
    float xy[2 * 2 * MESH_MAX];
    Color c[2 * MESH_MAX];
    for (int j = 0; j < rows; j++) {
        for (int i = 0; i <= cols; i++)
            for (int d = 0; d < 2; d++) {
                int k = 2 * i + d;
                float u = (float)i / cols, v = (float)(j + d) / rows;
                xy[2 * k] = x + w * u;
                xy[2 * k + 1] = y + h * v;
                c[k] = fn(u * 2 - 1, v * 2 - 1);
            }
        gfx_strip(xy, c, 2 * (cols + 1));
    }
}

/* a cool glow a little above the middle */
static Color glow(float u, float v) {
    float d = sqrtf(u * u * 0.7f + (v + 0.3f) * (v + 0.3f) * 0.9f);
    float a = clamp01(1 - d / 1.1f);
    return RGBA(118, 168, 222, (u8)(a * a * 74));
}

/* dark towards the edge, shaped like a tube: a squarish oval */
static Color tube(float u, float v) {
    float au = fabsf(u) * 1.02f, av = fabsf(v) * 1.04f;
    float d = powf(powf(au, 5) + powf(av, 5), 0.2f);
    float e = clamp01((d - 0.6f) / 0.42f);
    float r = clamp01((sqrtf(u * u * 0.8f + v * v) - 0.35f) / 0.9f);
    float a = 1 - (1 - e * e) * (1 - 0.5f * r * r);
    return RGBA(0, 0, 0, (u8)(clamp01(a) * 255));
}

void ui_backdrop(void) {
    gfx_rect_gradient(0, 0, SCREEN_W, SCREEN_H, RGB(28, 36, 49), RGB(12, 16, 22));
    mesh(0, 0, SCREEN_W, SCREEN_H, 12, 8, glow);
    for (int y = 0; y < SCREEN_H; y += 4) gfx_rect(0, y + 2, SCREEN_W, 2, RGBA(0, 0, 0, 52));
    mesh(0, 0, SCREEN_W, SCREEN_H, 24, 18, tube);
}

/* ---- boxes ---- */

#define CORNER_SEG 4
#define RR_N (4 * (CORNER_SEG + 1))

/* The outline of a rounded box, clockwise from the top-right corner */
static void round_points(float x, float y, float w, float h, float r, float *px, float *py) {
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 0) r = 0;
    const float cx[4] = { x + w - r, x + w - r, x + r, x + r };
    const float cy[4] = { y + r, y + h - r, y + h - r, y + r };
    int n = 0;
    for (int k = 0; k < 4; k++)
        for (int i = 0; i <= CORNER_SEG; i++) {
            float a = (float)M_PI * (-0.5f + 0.5f * k + 0.5f * i / CORNER_SEG);
            px[n] = cx[k] + r * cosf(a);
            py[n] = cy[k] + r * sinf(a);
            n++;
        }
}

void ui_outline(float x, float y, float w, float h, float r, float t, Color c) {
    if (w <= 0 || h <= 0 || c.a == 0) return;
    if (2 * t >= w || 2 * t >= h) { gfx_round_rect(x, y, w, h, r, c); return; }
    float ox[RR_N], oy[RR_N], ix[RR_N], iy[RR_N];
    round_points(x, y, w, h, r, ox, oy);
    round_points(x + t, y + t, w - 2 * t, h - 2 * t, r - t > 0 ? r - t : 0, ix, iy);
    float xy[4 * (RR_N + 1)];
    Color col[2 * (RR_N + 1)];
    for (int i = 0; i <= RR_N; i++) {
        int k = i % RR_N;
        xy[4 * i] = ox[k]; xy[4 * i + 1] = oy[k];
        xy[4 * i + 2] = ix[k]; xy[4 * i + 3] = iy[k];
        col[2 * i] = col[2 * i + 1] = c;
    }
    gfx_strip(xy, col, 2 * (RR_N + 1));
}

static void shadow(float x, float y, float w, float h, float r, float spread) {
    for (int i = 4; i >= 1; i--) {
        float s = spread * i / 4;
        gfx_round_rect(x - s, y - s, w + 2 * s, h + 2 * s, r + s, RGBA(0, 0, 0, 34));
    }
}

void ui_panel(float x, float y, float w, float h) {
    shadow(x, y + 6, w, h, 6, 8);
    ui_outline(x - 3, y - 3, w + 6, h + 6, 6, 2, RGBA(200, 208, 220, 140));
    ui_outline(x - 1, y - 1, w + 2, h + 2, 5, 1, RGB(10, 13, 16));
    gfx_round_rect_gradient(x, y, w, h, 4, RGBA(26, 46, 50, 240), RGBA(14, 22, 26, 247));
    /* lit a little above the middle, darker toward the sides */
    float h1 = h * 0.35f;
    gfx_rect_gradient(x, y + 3, w, h1, RGBA(40, 72, 74, 0), RGBA(40, 72, 74, 150));
    gfx_rect_gradient(x, y + 3 + h1, w, h * 0.4f, RGBA(40, 72, 74, 150), RGBA(40, 72, 74, 0));
    float e = fminf(14, fminf(w, h) * 0.3f);
    gfx_rect_hgradient(x, y + 3, e, h - 6, RGBA(0, 0, 0, 70), RGBA(0, 0, 0, 0));
    gfx_rect_hgradient(x + w - e, y + 3, e, h - 6, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 70));
    gfx_rect(x + 4, y + 1, w - 8, 1, RGBA(255, 255, 255, 46));
    ui_outline(x, y, w, h, 4, 1, RGBA(255, 255, 255, 30));
}

void ui_row(float x, float y, float w, float h) {
    gfx_rect_gradient(x, y, w, h, RGBA(60, 76, 96, 228), RGBA(30, 42, 56, 236));
    gfx_rect(x + 1, y + 1, w - 2, 1, RGBA(255, 255, 255, 40));
    ui_outline(x, y, w, h, 2, 1, RGBA(215, 225, 240, 153));
}

void ui_button(float x, float y, float w, float h, bool gold) {
    ui_outline(x - 2, y - 2, w + 4, h + 4, 5, 1, gold ? RGBA(230, 200, 70, 115) : RGBA(170, 178, 190, 56));
    ui_outline(x - 1, y - 1, w + 2, h + 2, 4, 1, gold ? RGBA(40, 32, 4, 230) : RGBA(10, 12, 16, 204));
    Color c0 = gold ? RGBA(255, 248, 178, 240) : RGBA(235, 240, 248, 107);
    Color c1 = gold ? RGBA(244, 218, 98, 232) : RGBA(170, 180, 195, 56);
    Color c2 = gold ? RGBA(222, 192, 70, 232) : RGBA(120, 130, 145, 77);
    float m = h / 2;
    gfx_rect(x, y, w, h, RGBA(8, 10, 14, 130));
    gfx_round_rect_gradient(x, y, w, m, 2, c0, c1);
    gfx_rect_gradient(x, y + m, w, h - m, c1, c2);
    ui_outline(x, y, w, h, 2, 1, gold ? RGBA(255, 250, 200, 204) : RGBA(235, 240, 250, 140));
    gfx_rect(x + 2, y + 1, w - 4, 1, gold ? RGBA(255, 255, 230, 204) : RGBA(255, 255, 255, 128));
}

void ui_ring(float x, float y, float w, float h, bool glow_) {
    if (glow_) {
        float pulse = 0.75f + 0.25f * sinf(video_frame_count() * 0.08f);
        for (int k = 3; k >= 1; k--)
            ui_outline(x - 5 - k * 2, y - 5 - k * 2, w + 10 + k * 4, h + 10 + k * 4, 4 + k * 2, 2.5f,
                       RGBA(240, 210, 80, (u8)(40 * pulse / k)));
        ui_outline(x - 5, y - 5, w + 10, h + 10, 1, 2, RGB(106, 90, 16));
    }
    ui_outline(x - 3, y - 3, w + 6, h + 6, 0, 3, C_GOLD);
    ui_outline(x, y, w, h, 0, 1, RGBA(106, 90, 16, 200));
}

void ui_desc_bar(float x, float y, float w, float h) {
    ui_outline(x - 1, y - 1, w + 2, h + 2, 4, 1, RGB(11, 11, 32));
    gfx_round_rect_gradient(x, y, w, h, 3, RGB(42, 34, 140), RGB(59, 44, 182));
    /* a fine grid fading in from the right */
    const float cell = 7, span = w * 0.3f, x0 = x + w - span;
    Color g = RGBA(190, 190, 255, 41);
    for (float gy = y + 1 + cell / 2; gy < y + h - 2; gy += cell)
        gfx_rect_hgradient(x0, gy, span - 1, 1, ui_alpha(g, 0), g);
    for (float gx = x + w - 1 - cell / 2; gx > x0; gx -= cell) {
        float k = (gx - x0) / span;
        gfx_rect(gx, y + 2, 1, h - 4, ui_alpha(g, k * k));
    }
    ui_outline(x, y, w, h, 3, 1, RGBA(175, 175, 255, 153));
}

void ui_track(float x, float y, float w, float h) {
    gfx_rect(x, y, w, h, RGBA(4, 8, 10, 200));
    gfx_rect(x, y, w, 1, RGBA(0, 0, 0, 220));
    ui_outline(x - 1, y - 1, w + 2, h + 2, 1, 1, RGBA(200, 208, 220, 90));
}

void ui_gold_fill(float x, float y, float w, float h) {
    if (w <= 0 || h <= 0) return;
    gfx_rect_gradient(x, y, w, h / 2, RGB(255, 244, 150), RGB(244, 218, 98));
    gfx_rect_gradient(x, y + h / 2, w, h / 2, RGB(244, 218, 98), RGB(200, 168, 52));
    gfx_rect(x, y, w, 1, RGBA(255, 255, 230, 220));
}

void ui_progress(float x, float y, float w, float h, float frac) {
    ui_track(x, y, w, h);
    if (frac >= 0) {
        ui_gold_fill(x, y, w * clamp01(frac), h);
        return;
    }
    float seg = w * 0.25f, p = fmodf(video_frame_count() * 3.0f, w + seg) - seg;
    float s = p < 0 ? 0 : p, e = p + seg > w ? w : p + seg;
    ui_gold_fill(x + s, y, e - s, h);
}

void ui_scrollbar(float x, float y, float h, const ListState *l) {
    if (l->count <= l->rows) return;
    ui_track(x, y, 8, h);
    float th = h * l->rows / l->count;
    if (th < 14) th = 14;
    float ty = y + (h - th) * l->top / (l->count - l->rows);
    ui_gold_fill(x + 1, ty, 6, th);
}

/* ---- button hints ---- */

static float hint_draw(float x, float y, const char *hint, int n, bool draw) {
    const char *sep = strstr(hint, "  ");
    if (!sep || sep >= hint + n) {
        char s[64];
        snprintf(s, sizeof s, "%.*s", n, hint);
        if (draw) font_draw(x, y, FS_SMALL, C_TEXT_DIM, s);
        return font_width(s, FS_SMALL);
    }
    char key[16], lab[64];
    snprintf(key, sizeof key, "%.*s", (int)(sep - hint), hint);
    snprintf(lab, sizeof lab, "%.*s", (int)(hint + n - sep - 2), sep + 2);
    const float ch = 20;
    float kw = font_width(key, FS_KEY) + 12;
    if (kw < 22) kw = 22;
    if (draw) {
        bool gold = !strcmp(key, "A");
        float cy = y + (font_height(FS_SMALL) - ch) / 2;
        ui_button(x, cy, kw, ch, gold);
        font_draw_center(x + kw / 2, cy + (ch - font_height(FS_KEY)) / 2, FS_KEY, gold ? C_TEXT_DARK : C_TEXT, key);
        font_draw(x + kw + 7, y, FS_SMALL, C_TEXT_DIM, lab);
    }
    return kw + 7 + font_width(lab, FS_SMALL);
}

float ui_hint(float x, float y, const char *hint) { return hint_draw(x, y, hint, (int)strlen(hint), true); }

void ui_hints(float x, float y, const char *hints) {
    for (const char *p = hints; *p;) {
        const char *end = strstr(p, "    ");
        int n = end ? (int)(end - p) : (int)strlen(p);
        x += hint_draw(x, y, p, n, true) + 18;
        if (!end) break;
        p = end + 4;
    }
}

/* ---- the frame ---- */

void ui_title_bar(void) {
    float x = FRAME_X, y = TITLE_Y, w = FRAME_W, h = TITLE_H;
    ui_panel(x, y, w, h);
    const char *mark = "GC HOMEBREW HUB";
    float mh = font_height(FS_BIG), mx = x + 17;
    font_draw(mx, y + (h - mh) / 2, FS_BIG, C_TEXT, mark);
    float sx = mx + font_width(mark, FS_BIG) + 14, lh = font_height(FS_SMALL);
    gfx_rect(sx - 7, y + 14, 1, h - 28, RGBA(255, 255, 255, 60));
    font_draw(sx, y + h / 2 - lh - 1, FS_SMALL, C_TEXT_DIM, "Browser");
    font_draw(sx, y + h / 2 + 2, FS_SMALL, C_TEXT_DIM, "version " APP_VERSION);

    float rx = x + w - 17, ly1 = y + h / 2 - lh - 1, ly2 = y + h / 2 + 2;
    char line[160];
    if (store_busy()) {
        const char *name = store_job_name();
        if (*name) snprintf(line, sizeof line, "%s: %s", *store_step() ? store_step() : "Working", name);
        else snprintf(line, sizeof line, "%s", *store_step() ? store_step() : "Working");
        float lw = font_width(line, FS_SMALL);
        if (lw > 220) lw = 220;
        font_draw_clip(rx - lw, ly1, FS_SMALL, C_GOLD, line, 220);
        ui_progress(rx - 180, ly2 + 2, 180, 8, store_progress());
        return;
    }
    bool up = store_net_up();
    if (up) snprintf(line, sizeof line, "Online  %s", net_ip_string());
    else snprintf(line, sizeof line, "Offline");
    float lw = font_width(line, FS_SMALL);
    font_draw(rx - lw, ly1, FS_SMALL, C_TEXT, line);
    Color dot = up ? C_GOOD : C_BAD;
    gfx_round_rect(rx - lw - 12, ly1 + lh / 2 - 3, 6, 6, 3, dot);
    if (!catalog_have_card()) {             /* browsing only: say so in red, left of the network */
        const char *nc = "No SD card";
        float nx = rx - lw - 24 - font_width(nc, FS_SMALL);
        font_draw(nx, ly1, FS_SMALL, C_BAD, nc);
    }
    time_t now = time(NULL);
    struct tm *tm = now > 1600000000 ? localtime(&now) : NULL;
    if (tm) strftime(line, sizeof line, "%Y-%m-%d  %H:%M", tm);
    else snprintf(line, sizeof line, "%d apps  %d on the card", catalog_app_count(), catalog_installed_count());
    font_draw_right(rx, ly2, FS_SMALL, C_TEXT_DIM, line);
}

float ui_side_pane(const char *heading) {
    ui_panel(SIDE_X, BODY_Y, SIDE_W, BODY_BOTTOM - BODY_Y);
    float bx = SIDE_X + 8, by = BODY_Y + 8, bw = SIDE_W - 16, bh = 28;
    ui_desc_bar(bx, by, bw, bh);
    font_draw(bx + 8, by + (bh - font_height(FS_SMALL)) / 2, FS_SMALL, C_TEXT, heading);
    return by + bh + 12;
}

void ui_side_hints(const char *hints) {
    int n = 1;
    for (const char *p = hints; (p = strstr(p, "    ")); p += 4) n++;
    const float pitch = 25;
    float y = BODY_BOTTOM - 12 - n * pitch;
    gfx_rect(SIDE_X + 12, y - 8, SIDE_W - 24, 1, RGBA(255, 255, 255, 40));
    for (const char *p = hints; *p; y += pitch) {
        const char *end = strstr(p, "    ");
        int len = end ? (int)(end - p) : (int)strlen(p);
        hint_draw(SIDE_X + 12, y, p, len, true);
        if (!end) break;
        p = end + 4;
    }
}

void ui_side_line(float *y, const char *label, const char *value, Color vc) {
    float x = SIDE_X + 12, w = SIDE_W - 24;
    if (font_width(label, FS_SMALL) + font_width(value, FS_SMALL) + 10 <= w) {   /* both on one line */
        font_draw(x, *y, FS_SMALL, C_TEXT_FAINT, label);
        font_draw_right(x + w, *y, FS_SMALL, vc, value);
        *y += font_height(FS_SMALL) + 6;
        return;
    }
    font_draw_clip(x, *y, FS_SMALL, C_TEXT_FAINT, label, w);
    *y += font_height(FS_SMALL) + 1;
    font_draw_clip(x, *y, FS_SMALL, vc, value, w);
    *y += font_height(FS_SMALL) + 6;
}

void ui_desc(const char *line1, const char *line2) {
    ui_desc_bar(MAIN_X, DESC_Y, MAIN_W, DESC_H);
    float lh = font_height(FS_SMALL), y = DESC_Y + (DESC_H - 2 * lh - 3) / 2, tw = MAIN_W - 20;
    if (line1 && (!line2 || !*line2)) {     /* one line of text, wrapped onto two if it needs them */
        int n = font_draw_wrapped(0, 0, FS_SMALL, C_TEXT, line1, tw, 2, false);
        font_draw_wrapped(MAIN_X + 10, DESC_Y + (DESC_H - n * lh - (n - 1) * 2) / 2, FS_SMALL, C_TEXT, line1, tw, 2, true);
        return;
    }
    if (line1) font_draw_clip(MAIN_X + 10, y, FS_SMALL, C_TEXT, line1, tw);
    if (line2) font_draw_clip(MAIN_X + 10, y + lh + 3, FS_SMALL, RGB(212, 212, 255), line2, tw);
}

/* ---- lists ---- */

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

void ui_list_row(int r, bool selected, bool focus, float *x, float *y) {
    *x = MAIN_X;
    *y = BODY_Y + 3 + r * ROW_PITCH;
    ui_row(*x, *y, ROW_W, ROW_H);
    if (selected && focus) ui_ring(*x, *y, ROW_W, ROW_H, false);
    else if (selected) ui_outline(*x - 2, *y - 2, ROW_W + 4, ROW_H + 4, 0, 1, ui_alpha(C_GOLD, 0.6f));
}

void ui_row_status(float y, const char *text, Color c) {
    font_draw_right(MAIN_X + ROW_W - 12, y + (ROW_H - font_height(FS_SMALL)) / 2, FS_SMALL, c, text);
}

void ui_initials(const char *name, char out[3]) {
    int n = 0;
    bool word = false;
    for (const char *p = name; *p && n < 2; p++) {
        bool alnum = isalnum((unsigned char)*p);
        if (alnum && !word) out[n++] = (char)toupper((unsigned char)*p);
        word = alnum;
    }
    if (n == 1) {
        const char *p = name;
        while (*p && !isalnum((unsigned char)*p)) p++;
        if (*p) p++;
        while (*p && !isalnum((unsigned char)*p)) p++;
        if (*p) out[n++] = (char)toupper((unsigned char)*p);
    }
    if (n == 0) out[n++] = '?';
    out[n] = 0;
}

void ui_plate(float x, float y, float w, float h, const char *name) {
    gfx_rect_gradient(x, y, w, h, RGB(30, 54, 60), RGB(12, 20, 24));
    gfx_rect(x + 1, y + 1, w - 2, 1, RGBA(255, 255, 255, 50));
    ui_outline(x, y, w, h, 1, 1, RGBA(200, 208, 220, 120));
    char ini[3];
    ui_initials(name, ini);
    font_draw_center(x + w / 2, y + (h - font_height(FS_BODY)) / 2, FS_BODY, C_TEXT, ini);
}

/* ---- pictograms ---- */

static void arc(float cx, float cy, float r, float t, float a0, float a1, Color c) {
    enum { SEG = 20 };
    float xy[4 * (SEG + 1)];
    Color col[2 * (SEG + 1)];
    for (int i = 0; i <= SEG; i++) {
        float a = a0 + (a1 - a0) * i / SEG;
        xy[4 * i] = cx + r * cosf(a);           xy[4 * i + 1] = cy + r * sinf(a);
        xy[4 * i + 2] = cx + (r - t) * cosf(a); xy[4 * i + 3] = cy + (r - t) * sinf(a);
        col[2 * i] = col[2 * i + 1] = c;
    }
    gfx_strip(xy, col, 2 * (SEG + 1));
}

void ui_icon(int icon, float cx, float cy, float s, Color c) {
    float t = s * 0.12f;
    if (t < 1.5f) t = 1.5f;
    switch (icon) {
    case ICON_STORE: {        /* four app tiles */
        float g = s * 0.1f, q = (s * 0.84f - g) / 2, x0 = cx - s * 0.42f, y0 = cy - s * 0.42f;
        for (int i = 0; i < 4; i++)
            gfx_round_rect(x0 + (i % 2) * (q + g), y0 + (i / 2) * (q + g), q, q, s * 0.06f, c);
        break;
    }
    case ICON_INSTALLED: {    /* an SD card */
        float x0 = cx - s * 0.34f, x1 = cx + s * 0.34f, y0 = cy - s * 0.46f, y1 = cy + s * 0.46f, cut = s * 0.22f;
        float card[] = { x0, y0 + cut, x0 + cut, y0, x1, y0, x1, y1, x0, y1 };
        gfx_poly(card, 5, c);
        for (int i = 0; i < 3; i++)
            gfx_rect(x0 + cut + s * 0.04f + i * s * 0.13f, y0 + s * 0.08f, s * 0.07f, s * 0.18f, RGBA(0, 0, 0, (u8)(c.a * 0.5f)));
        break;
    }
    case ICON_SOURCES:        /* a globe */
        ui_outline(cx - s * 0.45f, cy - s * 0.45f, s * 0.9f, s * 0.9f, s * 0.45f, t, c);
        ui_outline(cx - s * 0.2f, cy - s * 0.45f, s * 0.4f, s * 0.9f, s * 0.2f, t * 0.8f, c);
        gfx_rect(cx - s * 0.44f, cy - t * 0.4f, s * 0.88f, t * 0.8f, c);
        break;
    case ICON_SETTINGS:       /* a gear */
        for (int i = 0; i < 6; i++) {
            float a = i * (float)M_PI / 3, ca = cosf(a), sa = sinf(a), r0 = s * 0.25f, r1 = s * 0.47f, hw = s * 0.1f;
            float tooth[] = { cx + ca * r0 - sa * hw, cy + sa * r0 + ca * hw, cx + ca * r1 - sa * hw, cy + sa * r1 + ca * hw,
                              cx + ca * r1 + sa * hw, cy + sa * r1 - ca * hw, cx + ca * r0 + sa * hw, cy + sa * r0 - ca * hw };
            gfx_poly(tooth, 4, c);
        }
        ui_outline(cx - s * 0.33f, cy - s * 0.33f, s * 0.66f, s * 0.66f, s * 0.33f, s * 0.17f, c);
        break;
    case ICON_EXIT:           /* the power sign */
        arc(cx, cy + s * 0.04f, s * 0.42f, t, (float)M_PI * -0.32f, (float)M_PI * 1.32f, c);
        gfx_rect(cx - t / 2, cy - s * 0.48f, t, s * 0.44f, c);
        break;
    }
}

void ui_count_badge(float rx, float y, int count) {
    if (count <= 0) return;
    char s[8];
    snprintf(s, sizeof s, "%d", count > 99 ? 99 : count);
    float w = font_width(s, FS_KEY) + 10, h = 18;
    if (w < h) w = h;
    ui_outline(rx - w - 1, y - 1, w + 2, h + 2, h / 2 + 1, 1, RGB(40, 32, 4));
    gfx_round_rect_gradient(rx - w, y, w, h, h / 2, RGB(255, 244, 150), RGB(214, 182, 60));
    font_draw_center(rx - w / 2, y + (h - font_height(FS_KEY)) / 2, FS_KEY, C_TEXT_DARK, s);
}

/* ---- toasts ---- */

#define TOASTS 3
static struct { char text[160]; bool error; int ttl; } toasts[TOASTS];

void ui_toast(const char *text, bool error) {
    log_printf("%s %s", error ? "Error:" : "Shown:", text);
    memmove(&toasts[1], &toasts[0], sizeof toasts[0] * (TOASTS - 1));
    snprintf(toasts[0].text, sizeof toasts[0].text, "%s", text);
    toasts[0].error = error;
    toasts[0].ttl = error ? 300 : 200;
}

void ui_toasts_update_draw(void) {
    char m[160]; bool err;
    while (store_take_message(m, sizeof m, &err)) ui_toast(m, err);
    float y = DOCK_Y - 48;
    for (int i = 0; i < TOASTS; i++) {
        if (toasts[i].ttl <= 0) continue;
        toasts[i].ttl--;
        if (toasts[i].ttl < 20 && (toasts[i].ttl / 3) % 2) continue;   /* blinks out */
        float w = font_width(toasts[i].text, FS_SMALL) + 44, h = 32;
        if (w > FRAME_W - 40) w = FRAME_W - 40;
        float x = (SCREEN_W - w) / 2;
        ui_panel(x, y, w, h);
        Color c = toasts[i].error ? C_BAD : C_GOLD;
        gfx_rect(x + 12, y + h / 2 - 3, 6, 6, c);
        font_draw_clip(x + 26, y + (h - font_height(FS_SMALL)) / 2, FS_SMALL, toasts[i].error ? C_BAD : C_TEXT,
                       toasts[i].text, w - 38);
        y -= h + 12;
    }
}

/* ---- confirm ---- */

static struct { bool active; char title[64], text[200], yes[24], no[24]; } cf;

void ui_confirm_open(const char *title, const char *text, const char *yes, const char *no) {
    cf.active = true;
    snprintf(cf.title, sizeof cf.title, "%s", title);
    snprintf(cf.text, sizeof cf.text, "%s", text);
    snprintf(cf.yes, sizeof cf.yes, "%s", yes);
    snprintf(cf.no, sizeof cf.no, "%s", no);
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
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, 150));
    /* three lines fit the usual size; a longer question makes it taller */
    float w = 440;
    int lines = font_draw_wrapped(0, 0, FS_BODY, C_TEXT_DIM, cf.text, w - 44, 6, false);
    float h = 196 + (lines > 3 ? (lines - 3) * font_line_pitch(FS_BODY) : 0);
    float x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2;
    ui_panel(x, y, w, h);
    ui_desc_bar(x + 12, y + 12, w - 24, 32);
    font_draw(x + 22, y + 12 + (32 - font_height(FS_BODY)) / 2, FS_BODY, C_TEXT, cf.title);
    font_draw_wrapped(x + 22, y + 58, FS_BODY, C_TEXT_DIM, cf.text, w - 44, 6, true);
    /* the two answers, as glass buttons; the gold ring is on yes */
    float bw = 160, bh = 34, by = y + h - bh - 16, gap = 24, bx = x + (w - 2 * bw - gap) / 2;
    char yes[40], no[40];
    snprintf(yes, sizeof yes, "A  %s", cf.yes);
    snprintf(no, sizeof no, "B  %s", cf.no);
    const char *labels[2] = { yes, no };
    for (int i = 0; i < 2; i++) {
        float px = bx + i * (bw + gap);
        ui_button(px, by, bw, bh, false);
        if (i == 0) ui_ring(px, by, bw, bh, true);
        float hw = hint_draw(0, 0, labels[i], (int)strlen(labels[i]), false);
        hint_draw(px + (bw - hw) / 2, by + (bh - font_height(FS_SMALL)) / 2, labels[i], (int)strlen(labels[i]), true);
    }
}

void ui_format_size(unsigned long long b, char *out, size_t cap) {
    if (b >= (1ull << 20)) snprintf(out, cap, "%.1f MB", b / 1048576.0);
    else if (b >= 1024) snprintf(out, cap, "%llu KB", b / 1024);
    else snprintf(out, cap, "%llu bytes", b);
}
