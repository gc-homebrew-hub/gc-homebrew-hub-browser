#include "osk.h"
#include "ui.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define COLS 12
#define ROWS 4
static const char *rows[ROWS] = {
    "1234567890-_",
    "qwertyuiop./",
    "asdfghjkl:~@",
    "zxcvbnm?=&%#",
};
static const char *specials[] = { "http://", "https://", ".json", "Space", "Done", "Cancel" };
#define NSPECIAL 6

static struct { bool active; char title[48]; char text[300]; int maxlen; int row, col; bool caps; } k;

void osk_open(const char *title, const char *initial, int maxlen) {
    k.active = true;
    snprintf(k.title, sizeof k.title, "%s", title);
    snprintf(k.text, sizeof k.text, "%s", initial);
    k.maxlen = maxlen < (int)sizeof k.text - 1 ? maxlen : (int)sizeof k.text - 1;
    k.row = 1; k.col = 0; k.caps = false;
}

bool osk_active(void) { return k.active; }
const char *osk_text(void) { return k.text; }

static void put(const char *s) {
    size_t n = strlen(k.text);
    for (; *s && (int)n < k.maxlen; s++) k.text[n++] = k.caps ? (char)toupper((unsigned char)*s) : *s;
    k.text[n] = 0;
}

static void backspace(void) { size_t n = strlen(k.text); if (n) k.text[n - 1] = 0; }

int osk_update(u32 p) {
    if (!k.active) return 0;
    int ncols = k.row == ROWS ? NSPECIAL : COLS;
    if (p & BTN_LEFT) k.col = (k.col + ncols - 1) % ncols;
    if (p & BTN_RIGHT) k.col = (k.col + 1) % ncols;
    if (p & BTN_UP) k.row = (k.row + ROWS) % (ROWS + 1);
    if (p & BTN_DOWN) k.row = (k.row + 1) % (ROWS + 1);
    ncols = k.row == ROWS ? NSPECIAL : COLS;
    if (k.col >= ncols) k.col = ncols - 1;
    if (p & BTN_B) backspace();
    if (p & BTN_Y) k.caps = !k.caps;
    if (p & BTN_Z) put(" ");
    if (p & BTN_START) { k.active = false; return 1; }
    if (p & BTN_A) {
        if (k.row < ROWS) { char s[2] = { rows[k.row][k.col], 0 }; put(s); }
        else switch (k.col) {
            case 3: put(" "); break;
            case 4: k.active = false; return 1;
            case 5: k.active = false; return -1;
            default: { bool c = k.caps; k.caps = false; put(specials[k.col]); k.caps = c; }
        }
    }
    return 0;
}

static void key_cap(float x, float y, float w, float h, bool hot) {
    Color ac = ui_accent();
    if (hot) gfx_round_rect(x - 2, y - 2, w + 4, h + 4, 9, ui_alpha(ac, 0.4f));
    ui_glass(x, y, w, h, 7, hot ? RGBA(ac.r, ac.g, ac.b, 200) : C_TILE, 1.0f);
}

void osk_draw(void) {
    if (!k.active) return;
    Color ac = ui_accent();
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 10, 150));
    float w = 540, h = 330, x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2;
    gfx_round_rect(x - 3, y - 3, w + 6, h + 6, 17, ui_alpha(ac, 0.3f));
    ui_glass(x, y, w, h, 14, ui_mix(RGBA(36, 28, 90, 235), RGBA(ac.r, ac.g, ac.b, 235), 0.15f), 1.0f);
    font_draw_shadow(x + 20, y + 14, FS_BODY, C_TEXT_DIM, k.title);
    /* the text field */
    gfx_round_rect(x + 20, y + 44, w - 40, 34, 8, RGBA(0, 0, 0, 130));
    gfx_rect(x + 28, y + 76, w - 56, 1, ui_alpha(ac, 0.7f));
    float tw = font_width(k.text, FS_BODY);
    float tx = x + 30, maxw = w - 60;
    if (tw > maxw) tx -= tw - maxw;
    font_draw(tx, y + 50, FS_BODY, C_TEXT, k.text);
    if ((video_frame_count() / 30) % 2 == 0) gfx_rect(tx + tw + 2, y + 50, 2, font_height(FS_BODY), ac);
    /* keys */
    float kx = x + 20, ky = y + 94, kw = (w - 40) / COLS, kh = 40;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            bool hot = k.row == r && k.col == c;
            float px = kx + c * kw, py = ky + r * kh;
            key_cap(px + 2, py + 2, kw - 4, kh - 4, hot);
            char s[2] = { k.caps ? (char)toupper((unsigned char)rows[r][c]) : rows[r][c], 0 };
            font_draw_center(px + kw / 2, py + 8, FS_BODY, C_TEXT, s);
        }
    float sy = ky + ROWS * kh + 6, sw = (w - 40) / NSPECIAL;
    for (int c = 0; c < NSPECIAL; c++) {
        bool hot = k.row == ROWS && k.col == c;
        float px = kx + c * sw;
        key_cap(px + 2, sy + 2, sw - 4, kh - 4, hot);
        font_draw_center(px + sw / 2, sy + 10, FS_SMALL, C_TEXT, specials[c]);
    }
    ui_hints(x + 20, y + h - 28, "A  Type    B  Delete    Y  Caps    Z  Space    Start  Done", 1.0f);
}
