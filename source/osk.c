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

/* A key: grey glass, the gold ring on the one under the cursor */
static void key_cap(float x, float y, float w, float h, bool hot, const char *label, float scale) {
    ui_button(x, y, w, h, false);
    if (hot) ui_ring(x, y, w, h, false);
    font_draw_center(x + w / 2, y + (h - font_height(scale)) / 2, scale, C_TEXT, label);
}

void osk_draw(void) {
    if (!k.active) return;
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, 150));
    float w = 548, h = 338, x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2;
    ui_panel(x, y, w, h);
    ui_desc_bar(x + 12, y + 12, w - 24, 26);
    font_draw(x + 22, y + 12 + (26 - font_height(FS_SMALL)) / 2, FS_SMALL, C_TEXT, k.title);
    /* the text field: a dark groove, the caret in gold */
    float fx = x + 20, fy = y + 50, fw = w - 40, fh = 30;
    ui_track(fx, fy, fw, fh);
    float tw = font_width(k.text, FS_BODY), tx = fx + 10, maxw = fw - 24;
    if (tw > maxw) tx -= tw - maxw;
    float ty = fy + (fh - font_height(FS_BODY)) / 2;
    font_draw(tx, ty, FS_BODY, C_TEXT, k.text);
    if ((video_frame_count() / 30) % 2 == 0) gfx_rect(tx + tw + 2, ty, 2, font_height(FS_BODY), C_GOLD);
    /* keys */
    float kx = x + 20, ky = y + 94, kw = (w - 40) / COLS, kh = 38;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            char s[2] = { k.caps ? (char)toupper((unsigned char)rows[r][c]) : rows[r][c], 0 };
            key_cap(kx + c * kw + 3, ky + r * kh + 3, kw - 6, kh - 6, k.row == r && k.col == c, s, FS_BODY);
        }
    float sy = ky + ROWS * kh + 4, sw = (w - 40) / NSPECIAL;
    for (int c = 0; c < NSPECIAL; c++)
        key_cap(kx + c * sw + 3, sy + 3, sw - 6, kh - 6, k.row == ROWS && k.col == c, specials[c], FS_SMALL);
    ui_hints(x + 20, y + h - 26, "A  Type    B  Delete    Y  Caps    Z  Space    Start  Done");
}
