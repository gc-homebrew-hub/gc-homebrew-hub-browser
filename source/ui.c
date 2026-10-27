#include "ui.h"
#include "store.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static Color accent = C_STORE;

void ui_set_accent(Color c) { accent = c; }
Color ui_accent(void) { return accent; }

Color ui_mix(Color a, Color b, float t) {
    return RGBA((u8)(a.r + (b.r - a.r) * t), (u8)(a.g + (b.g - a.g) * t),
                (u8)(a.b + (b.b - a.b) * t), (u8)(a.a + (b.a - a.a) * t));
}

Color ui_alpha(Color c, float a) {
    if (a < 0) a = 0;
    if (a > 1) a = 1;
    return RGBA(c.r, c.g, c.b, (u8)(c.a * a));
}

static unsigned hash(unsigned x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}

/* ---- backgrounds ---- */

/* The sections sit on a strict dark grid; some crossings light up like
 * pixels on a display, each on its own slow pulse. */
void ui_background(void) {
    const int cell = 32;
    gfx_rect_gradient(0, 0, SCREEN_W, SCREEN_H, C_GRID_TOP, C_GRID_BOTTOM);
    gfx_rect_gradient(0, 0, SCREEN_W, 240, ui_alpha(accent, 0.09f), ui_alpha(accent, 0));
    for (int x = 0; x <= SCREEN_W; x += cell)
        gfx_rect(x, 0, 1, SCREEN_H, ui_alpha(accent, (x / cell) % 4 == 0 ? 0.16f : 0.07f));
    for (int y = 0; y <= SCREEN_H; y += cell)
        gfx_rect(0, y, SCREEN_W, 1, ui_alpha(accent, (y / cell) % 4 == 0 ? 0.16f : 0.07f));
    float t = video_frame_count() * 0.035f;
    for (int gy = 0; gy <= SCREEN_H / cell; gy++)
        for (int gx = 0; gx <= SCREEN_W / cell; gx++) {
            unsigned h = hash((unsigned)(gy * 64 + gx) + 17u);
            if (h % 7) continue;
            float v = 0.5f + 0.5f * sinf(t * (0.6f + (h >> 8 & 7) * 0.1f) + (h >> 12 & 255) * 0.0245f);
            v = v * v * v;
            if (v < 0.05f) continue;
            float x = gx * cell + 0.5f, y = gy * cell + 0.5f;
            gfx_rect(x - 4, y - 4, 8, 8, ui_alpha(accent, 0.22f * v));
            gfx_rect(x - 1.5f, y - 1.5f, 3, 3, ui_alpha(ui_mix(accent, RGB(255, 255, 255), 0.6f), v));
        }
    /* a scan line drifting down the screen */
    float sy = fmodf(video_frame_count() * 0.8f, SCREEN_H + 120) - 60;
    gfx_rect_gradient(0, sy - 30, SCREEN_W, 30, ui_alpha(accent, 0), ui_alpha(accent, 0.06f));
    gfx_rect_gradient(0, sy, SCREEN_W, 30, ui_alpha(accent, 0.06f), ui_alpha(accent, 0));
}

/* The main menu: deep indigo, a spotlight behind the cube, a glowing floor
 * grid running toward the viewer, a few drifting sparks. */
void ui_menu_background(void) {
    int f = video_frame_count();
    gfx_rect_gradient(0, 0, SCREEN_W, SCREEN_H, C_BG_TOP, C_BG_BOTTOM);
    float pulse = 0.5f + 0.5f * sinf(f * 0.02f);
    gfx_circle(320, 236, 210, RGBA(140, 110, 255, 10));
    gfx_circle(320, 236, 150, RGBA(150, 120, 255, (u8)(14 + 6 * pulse)));
    gfx_circle(320, 236, 95, RGBA(170, 150, 255, (u8)(14 + 8 * pulse)));

    const float vx = 320, vy = 236, top = 312;
    Color line = RGB(150, 120, 255);
    for (int k = -14; k <= 14; k++) {
        float xb = vx + k * 72.0f;
        float t = (top - vy) / (SCREEN_H - vy);
        gfx_line(vx + (xb - vx) * t, top, xb, SCREEN_H, 1.2f, ui_alpha(line, 0.22f));
    }
    float phase = fmodf(f * 0.012f, 1.0f);
    for (int i = 0; i < 14; i++) {
        float z = 1.0f + i - phase;
        float y = vy + (SCREEN_H - vy) / z;
        if (y < top) continue;
        gfx_rect(0, y, SCREEN_W, 1.2f, ui_alpha(line, 0.3f * (y - top) / (SCREEN_H - top)));
    }
    Color fade = ui_mix(C_BG_TOP, C_BG_BOTTOM, top / SCREEN_H);
    gfx_rect_gradient(0, top - 2, SCREEN_W, 36, fade, ui_alpha(fade, 0));

    for (int i = 0; i < 26; i++) {
        unsigned h = hash((unsigned)i * 977u + 5u);
        float speed = 0.15f + (h & 15) * 0.02f;
        float x = (float)(h >> 4 & 511) * 1.25f;
        float y = fmodf((float)(h >> 13 & 511) - f * speed + 4000.0f, SCREEN_H + 20) - 10;
        float tw = 0.5f + 0.5f * sinf(f * 0.05f + i);
        gfx_rect(x, y, 2, 2, RGBA(220, 210, 255, (u8)(40 + 120 * tw)));
    }
}

/* ---- frame ---- */

void ui_header(const char *title, const char *right) {
    gfx_rect_gradient(0, 0, SCREEN_W, 58, RGBA(0, 0, 0, 160), RGBA(0, 0, 0, 80));
    gfx_rect_gradient(0, 0, SCREEN_W, 22, RGBA(255, 255, 255, 18), RGBA(255, 255, 255, 0));
    gfx_rect(0, 58, SCREEN_W, 2, ui_alpha(accent, 0.85f));
    gfx_rect_gradient(0, 60, SCREEN_W, 10, ui_alpha(accent, 0.25f), ui_alpha(accent, 0));
    Color core = ui_mix(accent, RGB(255, 255, 255), 0.3f);
    CubeStyle st = { ui_mix(C_CUBE, accent, 0.55f), RGBA(core.r, core.g, core.b, 230),
                     ui_mix(accent, RGB(255, 255, 255), 0.5f), 1.0f, video_frame_count() * -1.5f, NULL, NULL };
    gfx_glass_cube(UI_LEFT + 16, 30, 12, 26, video_frame_count() * 0.9f, 0, &st);
    font_draw_shadow(UI_LEFT + 44, 14, FS_BIG, C_TEXT, title);
    if (right && *right) font_draw_right(UI_RIGHT, 22, FS_SMALL, C_TEXT_DIM, right);
}

void ui_footer(const char *hints) {
    gfx_rect_gradient(0, UI_BOTTOM - 6, SCREEN_W, SCREEN_H - UI_BOTTOM + 6, RGBA(0, 0, 0, 50), RGBA(0, 0, 0, 130));
    gfx_rect(0, UI_BOTTOM - 6, SCREEN_W, 1, ui_alpha(accent, 0.35f));
    ui_hints(UI_LEFT, UI_BOTTOM + 2, hints, 1.0f);
}

static void gloss_round(float x, float y, float w, float h, float r, Color c) {
    gfx_round_rect(x, y, w, h, r, c);
    gfx_round_rect_gradient(x + 1, y + 1, w - 2, h * 0.5f, r - 1, RGBA(255, 255, 255, (u8)(c.a * 0.45f)),
                            RGBA(255, 255, 255, (u8)(c.a * 0.08f)));
}

/* The controller's own buttons: big green A, red B, grey kidney X/Y, ... */
static float button_glyph(float x, float cy, const char *key, int n, float a) {
    float th = font_height(FS_SMALL);
    if (n == 1 && (key[0] == 'A' || key[0] == 'B')) {
        bool isa = key[0] == 'A';
        float r = isa ? 9 : 7.5f;
        gloss_round(x, cy - r, 2 * r, 2 * r, r, ui_alpha(isa ? RGB(30, 186, 128) : RGB(228, 52, 64), a));
        char s[2] = { key[0], 0 };
        font_draw(x + r - font_width(s, FS_SMALL) / 2, cy - th / 2, FS_SMALL, ui_alpha(C_TEXT, a), s);
        return 2 * r;
    }
    if (n == 1 && key[0] == 'C') {     /* the yellow C-stick */
        gfx_circle(x + 8, cy, 8, ui_alpha(RGB(196, 160, 20), a));
        gloss_round(x + 2, cy - 6, 12, 12, 6, ui_alpha(RGB(250, 210, 50), a));
        return 16;
    }
    if (n == 5 && !strncmp(key, "D-pad", 5)) {
        Color c = ui_alpha(RGB(196, 196, 210), a);
        gfx_round_rect(x, cy - 3, 18, 6, 2, c);
        gfx_round_rect(x + 6, cy - 9, 6, 18, 2, c);
        gfx_rect(x + 7.5f, cy - 1.5f, 3, 3, ui_alpha(RGB(90, 90, 110), a));
        return 18;
    }
    if (n == 1 && strchr("XYZLR", key[0])) {
        Color c = key[0] == 'Z' ? RGB(118, 96, 232) : key[0] == 'L' || key[0] == 'R' ? RGB(168, 168, 186) : RGB(214, 214, 228);
        gloss_round(x, cy - 7.5f, 22, 15, 7, ui_alpha(c, a));
        char s[2] = { key[0], 0 };
        font_draw(x + 11 - font_width(s, FS_SMALL) / 2, cy - th / 2, FS_SMALL,
                  ui_alpha(key[0] == 'Z' ? C_TEXT : C_TEXT_DARK, a), s);
        return 22;
    }
    if (n == 5 && !strncmp(key, "Start", 5)) {
        const float sc = FS_SMALL * 0.8f;
        float w = font_width("START", sc) + 14;
        gloss_round(x, cy - 7, w, 14, 7, ui_alpha(RGB(168, 168, 186), a));
        font_draw(x + 7, cy - font_height(sc) / 2, sc, ui_alpha(C_TEXT_DARK, a), "START");
        return w;
    }
    char s[24];
    snprintf(s, sizeof s, "%.*s", n, key);
    font_draw_shadow(x, cy - th / 2, FS_SMALL, ui_alpha(ui_mix(accent, RGB(255, 255, 255), 0.35f), a), s);
    return font_width(s, FS_SMALL);
}

void ui_hints(float x, float y, const char *hints, float a) {
    float cy = y + font_height(FS_SMALL) / 2;
    const char *p = hints;
    while (*p) {
        const char *end = strstr(p, "    ");
        int n = end ? (int)(end - p) : (int)strlen(p);
        const char *sep = strstr(p, "  ");
        if (sep && sep < p + n) {
            x += button_glyph(x, cy, p, (int)(sep - p), a) + 6;
            const char *lab = sep + 2;
            char s[64];
            snprintf(s, sizeof s, "%.*s", (int)(p + n - lab), lab);
            font_draw_shadow(x, y, FS_SMALL, ui_alpha(C_TEXT_DIM, a), s);
            x += font_width(s, FS_SMALL);
        } else {
            char s[64];
            snprintf(s, sizeof s, "%.*s", n, p);
            font_draw_shadow(x, y, FS_SMALL, ui_alpha(C_TEXT_DIM, a), s);
            x += font_width(s, FS_SMALL);
        }
        if (!end) break;
        p = end + 4;
        x += 20;
    }
}

void ui_glass(float x, float y, float w, float h, float r, Color tint, float a) {
    gfx_round_rect(x + 3, y + 5, w, h, r, RGBA(0, 0, 0, (u8)(60 * a)));
    gfx_round_rect_gradient(x, y, w, h, r, ui_alpha(ui_mix(tint, RGBA(255, 255, 255, tint.a), 0.18f), a),
                            ui_alpha(ui_mix(tint, RGBA(0, 0, 0, tint.a), 0.3f), a));
    float gh = h * 0.5f;
    if (gh > 30) gh = 30;
    gfx_round_rect_gradient(x + 2, y + 2, w - 4, gh, r - 2, RGBA(255, 255, 255, (u8)(60 * a)),
                            RGBA(255, 255, 255, 0));
    gfx_rect(x + r, y + 1, w - 2 * r, 1, RGBA(255, 255, 255, (u8)(130 * a)));
    gfx_rect(x + r, y + h - 1, w - 2 * r, 1, RGBA(255, 255, 255, (u8)(30 * a)));
}

void ui_panel(float x, float y, float w, float h) {
    Color tint = ui_mix(RGBA(255, 255, 255, 26), ui_alpha(accent, 26 / 255.0f), 0.35f);
    ui_glass(x, y, w, h, 12, tint, 1.0f);
}

void ui_tag(float rx, float y, const char *text, Color c) {
    float w = font_width(text, FS_SMALL) + 14;
    gloss_round(rx - w, y - 2, w, font_height(FS_SMALL) + 4, 7, RGBA(c.r, c.g, c.b, 56));
    font_draw_shadow(rx - w + 7, y, FS_SMALL, c, text);
}

void ui_progress(float x, float y, float w, float h, float frac) {
    gfx_round_rect(x, y, w, h, h / 2, RGBA(0, 0, 0, 120));
    gfx_round_rect(x, y, w, h, h / 2, ui_alpha(accent, 0.15f));
    float s = 0, e = 0;
    if (frac >= 0) {
        if (frac > 1) frac = 1;
        e = w * frac;
    } else {
        float p = fmodf(video_frame_count() * 3.0f, w + 80) - 80;
        s = p < 0 ? 0 : p; e = p + 80 > w ? w : p + 80;
    }
    if (e - s > h) {
        gfx_round_rect(x + s - 2, y - 2, e - s + 4, h + 4, h / 2 + 2, ui_alpha(accent, 0.3f));
        gfx_round_rect_gradient(x + s, y, e - s, h, h / 2, ui_mix(accent, RGB(255, 255, 255), 0.45f), accent);
        gfx_round_rect(x + s + 2, y + 1, e - s - 4, h * 0.4f, h * 0.2f, RGBA(255, 255, 255, 90));
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
    ui_glass(UI_LEFT, y, UI_RIGHT - UI_LEFT, 30, 8, RGBA(0, 0, 0, 120), 1.0f);
    ui_led(UI_LEFT + 14, y + 15, accent, true);
    font_draw_clip(UI_LEFT + 26, y + 4, FS_SMALL, C_TEXT, line, 320);
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
    float pulse = 0.5f + 0.5f * sinf(video_frame_count() * 0.08f);
    gfx_round_rect(x - 2, y - 2, w + 4, h + 4, 10, ui_alpha(accent, 0.18f + 0.1f * pulse));
    ui_glass(x, y, w, h, 8, ui_alpha(accent, 0.5f), 1.0f);
    gfx_round_rect(x + 5, y + 7, 3, h - 14, 1.5f, ui_mix(accent, RGB(255, 255, 255), 0.6f));
}

void ui_badge(float x, float y, int count) {
    if (count <= 0) return;
    char s[8];
    snprintf(s, sizeof s, "%d", count > 99 ? 99 : count);
    float w = font_width(s, FS_SMALL) + 12;
    if (w < 22) w = 22;
    gfx_round_rect(x - 2, y - 2, w + 4, 24, 12, RGBA(255, 214, 90, 70));
    gloss_round(x, y, w, 20, 10, C_ACCENT);
    font_draw(x + (w - font_width(s, FS_SMALL)) / 2, y + 1, FS_SMALL, RGB(40, 30, 10), s);
}

void ui_led(float cx, float cy, Color c, bool pulse) {
    float v = pulse ? 0.55f + 0.45f * sinf(video_frame_count() * 0.12f) : 1.0f;
    gfx_circle(cx, cy, 7, ui_alpha(c, 0.25f * v));
    gfx_circle(cx, cy, 3.5f, ui_alpha(ui_mix(c, RGB(255, 255, 255), 0.35f), 0.4f + 0.6f * v));
    gfx_rect(cx - 1.5f, cy - 2, 2, 2, RGBA(255, 255, 255, (u8)(200 * v)));
}

void ui_soft_ellipse(float cx, float cy, float rx, float ry, Color c) {
    /* stacked, shrinking pills: the alpha adds up toward the middle */
    const int layers = 12;
    for (int i = 0; i < layers; i++) {
        float k = 1.0f - (float)i / layers;
        gfx_round_rect(cx - rx * k, cy - ry * k, 2 * rx * k, 2 * ry * k, ry * k,
                       RGBA(c.r, c.g, c.b, (u8)(c.a / (float)layers + 0.5f)));
    }
}

/* ---- icons ---- */

void ui_icon(int icon, float cx, float cy, float s, Color c) {
    Color dark = RGBA(0, 0, 0, (u8)(c.a * 0.45f));
    Color hi = RGBA(255, 255, 255, (u8)(c.a * 0.85f));
    switch (icon) {
    case ICON_SOURCES:      /* a stack of servers with their lights */
        for (int i = -1; i <= 1; i++) {
            float y = cy + i * s * 0.34f - s * 0.13f;
            gfx_round_rect(cx - s * 0.48f, y, s * 0.96f, s * 0.26f, s * 0.07f, c);
            gfx_rect(cx - s * 0.36f, y + s * 0.11f, s * 0.36f, s * 0.04f, dark);
            gfx_circle(cx + s * 0.3f, y + s * 0.13f, s * 0.05f, hi);
        }
        break;
    case ICON_INSTALLED: {  /* an SD card */
        float x0 = cx - s * 0.36f, x1 = cx + s * 0.36f, y0 = cy - s * 0.46f, y1 = cy + s * 0.46f, cut = s * 0.22f;
        float card[] = { x0, y0 + cut, x0 + cut, y0, x1, y0, x1, y1, x0, y1 };
        gfx_poly(card, 5, c);
        for (int i = 0; i < 4; i++)
            gfx_rect(x0 + cut + s * 0.02f + i * s * 0.12f, y0 + s * 0.06f, s * 0.07f, s * 0.2f, dark);
        gfx_round_rect(x0 + s * 0.1f, cy + s * 0.04f, s * 0.52f, s * 0.3f, s * 0.04f, hi);
        break;
    }
    case ICON_STORE: {      /* an arrow down into a tray */
        gfx_rect(cx - s * 0.09f, cy - s * 0.48f, s * 0.18f, s * 0.42f, c);
        float head[] = { cx - s * 0.3f, cy - s * 0.1f, cx + s * 0.3f, cy - s * 0.1f, cx, cy + s * 0.24f };
        gfx_poly(head, 3, c);
        gfx_rect(cx - s * 0.46f, cy + s * 0.36f, s * 0.92f, s * 0.11f, c);
        gfx_rect(cx - s * 0.46f, cy + s * 0.12f, s * 0.11f, s * 0.24f, c);
        gfx_rect(cx + s * 0.35f, cy + s * 0.12f, s * 0.11f, s * 0.24f, c);
        break;
    }
    case ICON_SETTINGS:     /* a gear */
        for (int i = 0; i < 8; i++) {
            float a = i * (float)M_PI / 4, ca = cosf(a), sa = sinf(a);
            float r0 = s * 0.28f, r1 = s * 0.48f, hw = s * 0.09f;
            float t[] = { cx + ca * r0 - sa * hw, cy + sa * r0 + ca * hw, cx + ca * r1 - sa * hw, cy + sa * r1 + ca * hw,
                          cx + ca * r1 + sa * hw, cy + sa * r1 - ca * hw, cx + ca * r0 + sa * hw, cy + sa * r0 - ca * hw };
            gfx_poly(t, 4, c);
        }
        gfx_circle(cx, cy, s * 0.34f, c);
        gfx_circle(cx, cy, s * 0.14f, dark);
        break;
    }
}

/* ---- apps as blocks ---- */

Color ui_app_color(const char *key) {
    static const Color pal[] = {
        RGB(70, 190, 255), RGB(90, 240, 140), RGB(196, 120, 255), RGB(255, 160, 56),
        RGB(255, 100, 190), RGB(80, 230, 230), RGB(250, 220, 80),
    };
    unsigned h = 5381;
    for (const char *p = key; *p; p++) h = h * 33 + (unsigned char)tolower((unsigned char)*p);
    return pal[hash(h) % (sizeof pal / sizeof pal[0])];
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

void ui_app_block(float x, float y, float s, const char *name, const char *key, float a) {
    Color c = ui_app_color(key);
    gfx_round_rect(x - 2, y - 2, s + 4, s + 4, s * 0.2f + 2, ui_alpha(c, 0.25f * a));
    ui_glass(x, y, s, s, s * 0.2f, RGBA(c.r, c.g, c.b, 190), a);
    char ini[3];
    ui_initials(name, ini);
    float sc = s * 0.46f / font_height(1.0f);
    font_draw_shadow(x + (s - font_width(ini, sc)) / 2, y + (s - font_height(sc)) / 2, sc, ui_alpha(C_TEXT, a), ini);
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
    float y = UI_BOTTOM - 42;
    for (int i = 0; i < TOASTS; i++) {
        if (toasts[i].ttl <= 0) continue;
        toasts[i].ttl--;
        float a = toasts[i].ttl < 30 ? toasts[i].ttl / 30.0f : 1.0f;
        float w = font_width(toasts[i].text, FS_SMALL) + 44;
        if (w > UI_RIGHT - UI_LEFT) w = UI_RIGHT - UI_LEFT;
        float x = (SCREEN_W - w) / 2;
        Color c = toasts[i].error ? C_BAD : accent;
        ui_glass(x, y, w, 28, 14, RGBA(c.r / 3, c.g / 3, c.b / 3, 225), a);
        gfx_round_rect(x, y + 26, w, 2, 1, ui_alpha(c, 0.7f * a));
        ui_led(x + 16, y + 14, c, false);
        font_draw_clip(x + 28, y + 5, FS_SMALL, RGBA(255, 255, 255, (u8)(255 * a)), toasts[i].text, w - 40);
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
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 10, 140));
    float w = 420, h = 170, x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2;
    gfx_round_rect(x - 3, y - 3, w + 6, h + 6, 17, ui_alpha(accent, 0.3f));
    ui_glass(x, y, w, h, 14, ui_mix(RGBA(40, 30, 96, 235), RGBA(accent.r, accent.g, accent.b, 235), 0.18f), 1.0f);
    font_draw_shadow(x + 24, y + 18, FS_BIG, C_TEXT, cf.title);
    gfx_rect(x + 24, y + 50, w - 48, 1, ui_alpha(accent, 0.6f));
    font_draw_wrapped(x + 24, y + 60, FS_BODY, C_TEXT_DIM, cf.text, w - 48, 3, true);
    char hint[80];
    snprintf(hint, sizeof hint, "A  %s    B  Cancel", cf.yes);
    ui_hints(x + 24, y + h - 32, hint, 1.0f);
}

void ui_format_size(unsigned long long b, char *out, size_t cap) {
    if (b >= (1ull << 20)) snprintf(out, cap, "%.1f MB", b / 1048576.0);
    else if (b >= 1024) snprintf(out, cap, "%llu KB", b / 1024);
    else snprintf(out, cap, "%llu bytes", b);
}
