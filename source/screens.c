/* screens.c - Sources, Installed, Homebrew Hub, Settings and the app details page. */
#include "screens.h"
#include "ui.h"
#include "osk.h"
#include "catalog.h"
#include "store.h"
#include "settings.h"
#include "net.h"
#include "dol.h"
#include "fsutil.h"
#include <gccore.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROW_H 46
#define LIST_Y 104
#define LIST_ROWS 7

/* Installed: the card as a grid of blocks, like the memory card screen */
#define GRID_COLS 6
#define GRID_ROWS 3
#define CELL_W    92
#define CELL_H    78
#define GRID_X    ((SCREEN_W - GRID_COLS * CELL_W) / 2)
#define GRID_Y    100

static Screen cur;
static ListState ls_src, ls_inst, ls_store, ls_set;
static bool updates_only;
static int store_cat;
static int pending_remove = -1;
static int pending_action;            /* what a confirm answers: 1 remove source, 2 update, 3 remove app, 4 exit */
static int pending_index;
static int enter_flash;               /* frames left of the flash after flying in from the cube */

/* details */
static int det_app, det_inst, det_btn, det_from;
enum { ACT_INSTALL, ACT_UPDATE, ACT_START, ACT_REMOVE, ACT_HOMEPAGE };
static int det_actions[5], det_nact;

static const char *cat_keys[] = { "", "games", "emulators", "media", "utilities", "demos" };
static const char *cat_names[] = { "All", "Games", "Emulators", "Media", "Utilities", "Demos" };
#define NCATS 6

static const char *online_text(void) {
    static char s[64];
    if (store_busy()) return "";
    if (store_net_up()) snprintf(s, sizeof s, "Online  %s", net_ip_string());
    else snprintf(s, sizeof s, "Offline");
    if (!catalog_have_card()) snprintf(s + strlen(s), sizeof s - strlen(s), "   No SD card");
    return s;
}

static void do_exit(void) {
    char path[160];
    fs_join(path, sizeof path, catalog_datadir(), "settings.ini");
    settings_save(path);
    if (g_settings.exit_action == 0) SYS_ResetSystem(SYS_HOTRESET, 0, 0);
    exit(0);
}

static void launch(const Installed *in) {
    if (!in) return;
    char path[300], err[128];
    snprintf(path, sizeof path, APPS_DIR "/%s/%s", in->folder, in->binary);
    char spath[160];
    fs_join(spath, sizeof spath, catalog_datadir(), "settings.ini");
    settings_save(spath);
    if (!dol_launch(path, err, sizeof err)) {
        char m[200];
        snprintf(m, sizeof m, "%s did not start: %s", in->name, err);
        ui_toast(m, true);
    }
}

/* ---- the block grid: sel is an item, top the first item of the top row ---- */

static void grid_clamp(ListState *l) {
    if (l->count <= 0) { l->sel = 0; l->top = 0; return; }
    if (l->sel >= l->count) l->sel = l->count - 1;
    if (l->sel < 0) l->sel = 0;
    int row = l->sel / GRID_COLS, top = l->top / GRID_COLS;
    if (row < top) top = row;
    if (row >= top + GRID_ROWS) top = row - GRID_ROWS + 1;
    l->top = top * GRID_COLS;
}

static void grid_nav(ListState *l, u32 p) {
    int n = l->count, s = l->sel;
    if (n <= 0) return;
    if (p & BTN_RIGHT) s = s + 1 < n ? s + 1 : 0;
    if (p & BTN_LEFT) s = s > 0 ? s - 1 : n - 1;
    if (p & BTN_DOWN) {
        if (s + GRID_COLS < n) s += GRID_COLS;
        else if (s / GRID_COLS < (n - 1) / GRID_COLS) s = n - 1;     /* into a shorter last row */
        else s %= GRID_COLS;                                        /* round to the top row */
    }
    if (p & BTN_UP) {
        if (s >= GRID_COLS) s -= GRID_COLS;
        else { s += (n - 1) / GRID_COLS * GRID_COLS; if (s >= n) s = n - 1; }
    }
    if (p & BTN_R) s += GRID_COLS * GRID_ROWS;
    if (p & BTN_L) s -= GRID_COLS * GRID_ROWS;
    l->sel = s;
    grid_clamp(l);
}

/* An app as a little glass cube: its initials on the front with a shine
 * sweeping over them, an arrow on top when there is an update. */
typedef struct { char ini[3]; Color c; bool update; float phase; } Block;

static void block_face(int face, float f, void *user) {
    const Block *b = user;
    if (face == CUBE_FRONT) {
        float sc = 44 / font_height(1.0f);
        font_draw_center(50, 50 - font_height(sc) / 2, sc, ui_alpha(C_TEXT, f), b->ini);
        float sx = fmodf(b->phase, 180) - 40;
        float x0 = sx < 3 ? 3 : sx, x1 = sx + 20 > 97 ? 97 : sx + 20;
        if (x1 > x0) gfx_rect(x0, 3, x1 - x0, 94, RGBA(255, 255, 255, (u8)(46 * f)));
    } else if (face == CUBE_TOP && b->update) {
        float up[] = { 50, 16, 80, 52, 20, 52 };
        gfx_poly(up, 3, ui_alpha(C_ACCENT, f));
        gfx_rect(38, 52, 24, 30, ui_alpha(C_ACCENT, f));
    }
}

static void block_cube(float cx, float cy, float size, float rx, float ry, const char *name, const char *key,
                       bool known, bool update, float alpha) {
    Block b;
    ui_initials(name, b.ini);
    b.c = known ? ui_app_color(key) : RGB(150, 150, 172);
    b.update = update;
    b.phase = video_frame_count() * 1.4f + (float)(key[0] * 37);
    CubeStyle st = { b.c, RGBA(0, 0, 0, 0), ui_mix(b.c, RGB(255, 255, 255), 0.55f), alpha, 0, block_face, &b };
    gfx_glass_cube(cx, cy, size, rx, ry, 0, &st);
}

/* ---- installed list filtering ---- */

static int inst_items[MAX_INSTALLED];
static int inst_nitems;

static void build_installed(void) {
    inst_nitems = 0;
    for (int i = 0; i < catalog_installed_count(); i++) {
        Installed *in = catalog_installed(i);
        if (updates_only && !(in->app >= 0 && catalog_app(in->app)->update)) continue;
        inst_items[inst_nitems++] = i;
    }
    ls_inst.count = inst_nitems;
    ls_inst.rows = GRID_ROWS;
    grid_clamp(&ls_inst);
}

static int store_items[MAX_APPS];
static int store_nitems;

static void build_store(void) {
    store_nitems = 0;
    for (int i = 0; i < catalog_app_count(); i++) {
        App *a = catalog_app(i);
        if (store_cat && strcasecmp(a->category, cat_keys[store_cat])) continue;
        store_items[store_nitems++] = i;
    }
    ls_store.count = store_nitems;
    ls_store.rows = LIST_ROWS;
    ui_list_clamp(&ls_store);
}

/* ---- details ---- */

static void open_details(int app, int inst, int from) {
    det_app = app; det_inst = inst; det_from = from; det_btn = 0;
    if (det_app >= 0 && det_inst < 0) det_inst = catalog_app(det_app)->installed;
    if (det_inst >= 0 && det_app < 0) det_app = catalog_installed(det_inst)->app;
    cur = SCR_DETAILS;
}

static void details_actions(void) {
    det_nact = 0;
    App *a = det_app >= 0 ? catalog_app(det_app) : NULL;
    if (a) det_inst = a->installed;           /* an install or removal may just have finished */
    bool installed = det_inst >= 0 && catalog_installed(det_inst) != NULL;
    if (a && !installed) det_actions[det_nact++] = ACT_INSTALL;
    if (a && installed && a->update) det_actions[det_nact++] = ACT_UPDATE;
    if (installed) det_actions[det_nact++] = ACT_START;
    if (a && installed && !a->update) det_actions[det_nact++] = ACT_INSTALL;   /* reinstall */
    if (installed) det_actions[det_nact++] = ACT_REMOVE;
    if (det_btn >= det_nact) det_btn = det_nact ? det_nact - 1 : 0;
}

static const char *action_label(int act) {
    switch (act) {
    case ACT_INSTALL: return det_inst >= 0 ? "Reinstall" : "Install";
    case ACT_UPDATE: return "Update";
    case ACT_START: return "Start";
    case ACT_REMOVE: return "Remove";
    default: return "";
    }
}

static int details_update(u32 p) {
    details_actions();
    if (p & BTN_B) return det_from;
    if (store_busy()) return -1;
    if (p & BTN_LEFT) det_btn = det_btn > 0 ? det_btn - 1 : det_nact - 1;
    if (p & BTN_RIGHT) det_btn = det_btn + 1 < det_nact ? det_btn + 1 : 0;
    if ((p & BTN_A) && det_nact) {
        switch (det_actions[det_btn]) {
        case ACT_INSTALL: case ACT_UPDATE:
            if (!catalog_have_card()) ui_toast("No SD card: insert one to install apps", true);
            else if (!store_net_up()) ui_toast("No network connection", true);
            else store_request_install(det_app);
            break;
        case ACT_START: launch(catalog_installed(det_inst)); break;
        case ACT_REMOVE: {
            char t[200];
            Installed *in = catalog_installed(det_inst);
            App *a = det_app >= 0 ? catalog_app(det_app) : NULL;
            snprintf(t, sizeof t, "Delete sd:/apps/%s from the card?%s", in->folder,
                     a && a->nto_root ? " Its data at the root of the card stays." : "");
            ui_confirm_open("Remove app", t, "Remove");
            pending_action = 3; pending_index = det_inst;
            break;
        }
        }
    }
    return -1;
}

static void details_draw(void) {
    App *a = det_app >= 0 ? catalog_app(det_app) : NULL;
    Installed *in = det_inst >= 0 ? catalog_installed(det_inst) : NULL;
    if (!a && !in) { cur = det_from; return; }
    const char *name = a ? a->name : in->name;
    char right[64] = "";
    if (a) snprintf(right, sizeof right, "Version %s", a->version);
    ui_header(name, right);
    float x = UI_LEFT, y = 68, w = UI_RIGHT - UI_LEFT;
    ui_panel(x, y, w, 300);
    {
        int f = video_frame_count();
        const char *key = a ? a->slug : in->slug;
        Color c = a ? ui_app_color(key) : RGB(150, 150, 172);
        float bx = x + w - 78, by = y + 84 + 4 * sinf(f * 0.04f);
        ui_soft_ellipse(bx, y + 168, 70, 10, ui_alpha(c, 0.5f));
        block_cube(bx, by, 36, 16, -26 + 22 * sinf(f * 0.015f), name, key, a != NULL, a && a->update, 1.0f);
    }
    float ty = y + 14, lx = x + 18;
    char line[200];
    if (a) {
        snprintf(line, sizeof line, "by %s", *a->author ? a->author : "unknown");
        font_draw_shadow(lx, ty, FS_BODY, C_TEXT_DIM, line); ty += font_height(FS_BODY) + 2;
    }
    if (in) {
        if (in->managed && *in->version) snprintf(line, sizeof line, "Installed: version %s in sd:/apps/%s", in->version, in->folder);
        else snprintf(line, sizeof line, "On the card in sd:/apps/%s", in->folder);
        font_draw_shadow(lx, ty, FS_SMALL, a && a->update ? C_ACCENT : C_GOOD, line); ty += font_height(FS_SMALL) + 2;
    }
    if (a) {
        char sz[32] = "size unknown";
        if (a->size) ui_format_size(a->size, sz, sizeof sz);
        Source *s = catalog_source(a->source);
        snprintf(line, sizeof line, "%s   %s   from %s", a->category[0] ? a->category : "app", sz, s ? s->name : "?");
        font_draw_shadow(lx, ty, FS_SMALL, C_TEXT_FAINT, line); ty += font_height(FS_SMALL) + 8;
        const char *desc = *a->description ? a->description : a->summary;
        int n = font_draw_wrapped(lx, ty, FS_BODY, C_TEXT, desc, w - 170, 8, true);
        ty += n * font_height(FS_BODY) * 1.05f + 6;
        if (a->nto_root) {
            snprintf(line, sizeof line, "Also puts %s at the root of the card.", a->to_root[0]);
            font_draw_shadow(lx, ty, FS_SMALL, C_TEXT_FAINT, line); ty += font_height(FS_SMALL) + 2;
        }
        if (*a->homepage) font_draw_clip(lx, ty, FS_SMALL, C_TEXT_FAINT, a->homepage, w - 36);
    } else {
        font_draw_wrapped(lx, ty + 8, FS_BODY, C_TEXT_DIM,
            "This app was not installed from a catalog, so there is nothing to update. You can start it or remove its folder.",
            w - 36, 4, true);
    }
    /* buttons */
    details_actions();
    float by = y + 300 + 14, bx = x;
    if (store_busy()) {
        char st[160];
        snprintf(st, sizeof st, "%s...", store_step());
        font_draw_shadow(x, by + 6, FS_BODY, C_TEXT, st);
        ui_progress(x + 220, by + 12, w - 220, 12, store_progress());
    } else {
        for (int i = 0; i < det_nact; i++) {
            const char *lab = action_label(det_actions[i]);
            float bw = font_width(lab, FS_BODY) + 36;
            bool hot = i == det_btn;
            Color base = det_actions[i] == ACT_REMOVE ? C_BAD : det_actions[i] == ACT_UPDATE ? C_ACCENT : ui_accent();
            if (hot) gfx_round_rect(bx - 4, by - 4, bw + 8, 46, 14, ui_alpha(base, 0.35f));
            ui_glass(bx, by, bw, 38, 12, RGBA(base.r, base.g, base.b, hot ? 200 : 50), 1.0f);
            font_draw_shadow(bx + 18, by + 8, FS_BODY, C_TEXT, lab);
            bx += bw + 12;
        }
    }
    ui_footer("Left/Right  Choose    A  Do it    B  Back");
}

/* ---- sources ---- */

static int sources_update(u32 p) {
    ls_src.count = catalog_source_count(); ls_src.rows = LIST_ROWS;
    ui_list_nav(&ls_src, p);
    if (p & BTN_B) return SCR_MENU;
    if (p & BTN_START) { store_request_refresh(); return -1; }
    if (store_busy()) return -1;
    Source *s = catalog_source(ls_src.sel);
    if ((p & BTN_A) && s) catalog_source_toggle(ls_src.sel);
    if (p & BTN_X) osk_open("Address of the source (its index.json)", "https://", 250);
    if ((p & BTN_Y) && s) {
        if (s->builtin) ui_toast("Built-in sources can be switched off but not removed", false);
        else {
            char t[200];
            snprintf(t, sizeof t, "Remove %s from the list?", s->name);
            ui_confirm_open("Remove source", t, "Remove");
            pending_action = 1; pending_index = ls_src.sel;
        }
    }
    return -1;
}

static void sources_draw(void) {
    ui_header("SOURCES", online_text());
    ui_busy_banner();
    float x = UI_LEFT, w = UI_RIGHT - UI_LEFT;
    ui_panel(x, LIST_Y, w, ROW_H * LIST_ROWS + 12);
    int n = catalog_source_count();
    if (!n) font_draw_center(SCREEN_W / 2, LIST_Y + 60, FS_BODY, C_TEXT_DIM, "No sources yet. Press X to add one.");
    for (int r = 0; r < LIST_ROWS && ls_src.top + r < n; r++) {
        int i = ls_src.top + r;
        Source *s = catalog_source(i);
        float y = LIST_Y + 6 + r * ROW_H;
        ui_row_highlight(x + 6, y, w - 12, ROW_H - 2, i == ls_src.sel);
        Color nc = s->enabled ? C_TEXT : C_TEXT_FAINT;
        ui_led(x + 26, y + ROW_H / 2 - 1, !s->enabled ? RGB(110, 110, 130) : s->napps < 0 ? C_BAD : s->napps > 0 ? C_GOOD : C_ACCENT,
               s->enabled && s->napps < 0);
        font_draw_clip(x + 42, y + 4, FS_BODY, nc, s->name, 310);
        font_draw_clip(x + 42, y + 4 + font_height(FS_BODY) - 2, FS_SMALL, C_TEXT_FAINT, s->url, 380);
        if (!s->enabled) ui_tag(x + w - 16, y + 12, "Off", C_TEXT_FAINT);
        else if (s->napps < 0) ui_tag(x + w - 16, y + 12, "Failed", C_BAD);
        else ui_tag(x + w - 16, y + 12, s->status, s->napps > 0 ? C_GOOD : C_TEXT_DIM);
        if (s->builtin) font_draw_right(x + w - 16, y + 30, FS_SMALL, C_TEXT_FAINT, "built in");
    }
    if (n && ls_src.sel < n && catalog_source(ls_src.sel)->napps < 0)
        font_draw_clip(x + 6, LIST_Y + ROW_H * LIST_ROWS + 20, FS_SMALL, C_BAD, catalog_source(ls_src.sel)->status, w - 12);
    ui_footer("A  On/Off    X  Add    Y  Remove    Start  Reload    B  Back");
}

/* ---- installed ---- */

static int installed_update(u32 p) {
    build_installed();
    grid_nav(&ls_inst, p);
    if (p & BTN_B) return SCR_MENU;
    if (p & BTN_Y) { updates_only = !updates_only; build_installed(); }
    if (p & BTN_X) store_request_refresh();
    if ((p & BTN_A) && inst_nitems) {
        int i = inst_items[ls_inst.sel];
        Installed *in = catalog_installed(i);
        if (updates_only) {
            if (store_busy()) return -1;
            App *a = catalog_app(in->app);
            char t[200];
            snprintf(t, sizeof t, "Update %s from %s to %s?", in->name, *in->version ? in->version : "?", a->version);
            ui_confirm_open("Update app", t, "Update");
            pending_action = 2; pending_index = in->app;
        } else open_details(in->app, i, SCR_INSTALLED);
    }
    return -1;
}

static void installed_draw(void) {
    build_installed();
    ui_header(updates_only ? "INSTALLED - UPDATES" : "INSTALLED", online_text());
    ui_busy_banner();
    int f = video_frame_count();
    Color ac = ui_accent();
    for (int slot = 0; slot < GRID_COLS * GRID_ROWS; slot++) {
        int idx = ls_inst.top + slot;
        float x0 = GRID_X + (slot % GRID_COLS) * CELL_W, y0 = GRID_Y + (slot / GRID_COLS) * CELL_H;
        float cx = x0 + CELL_W / 2.0f, cy = y0 + 32;
        if (idx >= inst_nitems) {       /* a free block */
            gfx_round_rect(cx - 19, cy - 19, 38, 38, 6, RGBA(255, 255, 255, 8));
            gfx_frame(cx - 19, cy - 19, 38, 38, 1, ui_alpha(ac, 0.2f));
            gfx_rect(cx - 1.5f, cy - 1.5f, 3, 3, ui_alpha(ac, 0.4f));
            continue;
        }
        Installed *in = catalog_installed(inst_items[idx]);
        App *a = in->app >= 0 ? catalog_app(in->app) : NULL;
        bool hot = idx == ls_inst.sel, upd = a && a->update;
        if (hot) {
            /* the cursor: a glow and four corner brackets around the cell */
            float pulse = 0.5f + 0.5f * sinf(f * 0.1f);
            float bx = x0 + 4, by = y0, bw = CELL_W - 8, bh = CELL_H - 2, L = 12;
            gfx_round_rect(bx, by, bw, bh, 10, ui_alpha(ac, 0.12f + 0.06f * pulse));
            Color br = ui_alpha(ui_mix(ac, RGB(255, 255, 255), 0.4f), 0.7f + 0.3f * pulse);
            gfx_rect(bx, by, L, 2, br); gfx_rect(bx, by, 2, L, br);
            gfx_rect(bx + bw - L, by, L, 2, br); gfx_rect(bx + bw - 2, by, 2, L, br);
            gfx_rect(bx, by + bh - 2, L, 2, br); gfx_rect(bx, by + bh - L, 2, L, br);
            gfx_rect(bx + bw - L, by + bh - 2, L, 2, br); gfx_rect(bx + bw - 2, by + bh - L, 2, L, br);
        }
        float size = hot ? 21 : 17, ry = hot ? f * 1.5f : -28 + 6 * sinf(f * 0.03f + idx);
        if (hot) cy -= 3 + 2 * sinf(f * 0.09f);
        block_cube(cx, cy, size, 20, ry, in->name, in->slug, a != NULL || in->managed, upd, hot ? 1.0f : 0.8f);
        if (upd) ui_led(cx + 26, cy - 22, C_ACCENT, true);
        float sc = FS_SMALL * 0.85f, nw = font_width(in->name, sc);
        Color nc = hot ? C_TEXT : C_TEXT_FAINT;
        if (nw <= CELL_W - 10) font_draw_shadow(cx - nw / 2, y0 + CELL_H - 20, sc, nc, in->name);
        else font_draw_clip(x0 + 5, y0 + CELL_H - 20, sc, nc, in->name, CELL_W - 10);
    }
    /* the selected app, like the memory card screen's info strip */
    float x = UI_LEFT, w = UI_RIGHT - UI_LEFT, py = GRID_Y + GRID_ROWS * CELL_H + 8, ph = UI_BOTTOM - 10 - py;
    ui_panel(x, py, w, ph);
    if (!inst_nitems) {
        font_draw_center(SCREEN_W / 2, py + ph / 2 - 10, FS_BODY, C_TEXT_DIM,
                         !catalog_have_card() ? "No SD card. Insert one (SD2SP2, or an SD adapter in slot A or B)." :
                         updates_only ? "Everything is up to date." : "No apps in sd:/apps yet. Visit the Homebrew Hub.");
    } else {
        Installed *in = catalog_installed(inst_items[ls_inst.sel]);
        App *a = in->app >= 0 ? catalog_app(in->app) : NULL;
        font_draw_clip(x + 18, py + 10, FS_BODY, C_TEXT, in->name, 360);
        char sub[112];
        if (in->managed && *in->version) snprintf(sub, sizeof sub, "Version %s   sd:/apps/%s", in->version, in->folder);
        else snprintf(sub, sizeof sub, "sd:/apps/%s/%s", in->folder, in->binary);
        font_draw_clip(x + 18, py + 12 + font_height(FS_BODY), FS_SMALL, C_TEXT_DIM, sub, w - 36);
        if (a && *a->summary) font_draw_clip(x + 18, py + 14 + font_height(FS_BODY) + font_height(FS_SMALL), FS_SMALL, C_TEXT_FAINT, a->summary, w - 36);
        if (a && a->update) { char t[40]; snprintf(t, sizeof t, "Update %s", a->version); ui_tag(x + w - 16, py + 12, t, C_ACCENT); }
        else if (a) ui_tag(x + w - 16, py + 12, "Up to date", C_GOOD);
        else ui_tag(x + w - 16, py + 12, "Not in a catalog", C_TEXT_FAINT);
        char pos[32];
        snprintf(pos, sizeof pos, "%d / %d", ls_inst.sel + 1, inst_nitems);
        font_draw_right(x + w - 16, py + ph - 22, FS_SMALL, C_TEXT_FAINT, pos);
    }
    ui_footer(updates_only ? "A  Update    Y  Show all    X  Reload    B  Back"
                           : "A  Details    Y  Updates only    X  Reload    B  Back");
}

/* ---- homebrew hub (the store) ---- */

static int storescr_update(u32 p) {
    build_store();
    ui_list_nav(&ls_store, p);
    if (p & BTN_B) return SCR_MENU;
    if (p & BTN_X) store_request_refresh();
    if (p & BTN_RIGHT) { store_cat = (store_cat + 1) % NCATS; ls_store.sel = 0; ls_store.top = 0; build_store(); }
    if (p & BTN_LEFT) { store_cat = (store_cat + NCATS - 1) % NCATS; ls_store.sel = 0; ls_store.top = 0; build_store(); }
    if ((p & BTN_A) && store_nitems) open_details(store_items[ls_store.sel], -1, SCR_STORE);
    return -1;
}

static void storescr_draw(void) {
    build_store();
    ui_header("HOMEBREW HUB", online_text());
    ui_busy_banner();
    float x = UI_LEFT, w = UI_RIGHT - UI_LEFT;
    /* category strip (the busy banner takes its place while the worker runs) */
    float cx = x;
    for (int i = 0; i < NCATS && !store_busy(); i++) {
        float cw = font_width(cat_names[i], FS_SMALL) + 18;
        bool hot = i == store_cat;
        Color ac = ui_accent();
        if (hot) gfx_round_rect(cx - 2, 68, cw + 4, 28, 12, ui_alpha(ac, 0.3f));
        ui_glass(cx, 70, cw, 24, 10, hot ? RGBA(ac.r, ac.g, ac.b, 180) : C_TILE, 1.0f);
        font_draw_shadow(cx + 9, 72, FS_SMALL, C_TEXT, cat_names[i]);
        cx += cw + 6;
    }
    float ly = LIST_Y;
    ui_panel(x, ly, w, ROW_H * LIST_ROWS + 12);
    if (!store_nitems) {
        const char *m = catalog_app_count() ? "Nothing in this category." :
                        store_net_up() ? "No apps loaded. Press X to reload the catalogs." :
                        "Offline and no saved catalog. Connect the network adapter and restart.";
        font_draw_center(SCREEN_W / 2, ly + 60, FS_BODY, C_TEXT_DIM, m);
    }
    for (int r = 0; r < LIST_ROWS && ls_store.top + r < store_nitems; r++) {
        App *a = catalog_app(store_items[ls_store.top + r]);
        float y = ly + 6 + r * ROW_H;
        ui_row_highlight(x + 6, y, w - 12, ROW_H - 2, ls_store.top + r == ls_store.sel);
        ui_app_block(x + 18, y + 5, ROW_H - 12, a->name, a->slug, 1.0f);
        font_draw_clip(x + 64, y + 4, FS_BODY, C_TEXT, a->name, 320);
        char sub[160];
        snprintf(sub, sizeof sub, "%s   %s", *a->author ? a->author : "", a->summary);
        font_draw_clip(x + 64, y + 4 + font_height(FS_BODY) - 2, FS_SMALL, C_TEXT_FAINT, sub, 380);
        if (a->update) { char t[40]; snprintf(t, sizeof t, "Update %s", a->version); ui_tag(x + w - 16, y + 12, t, C_ACCENT); }
        else if (a->installed >= 0) ui_tag(x + w - 16, y + 12, "Installed", C_GOOD);
        else { char sz[32]; if (a->size) ui_format_size(a->size, sz, sizeof sz); else snprintf(sz, sizeof sz, "v%s", a->version); ui_tag(x + w - 16, y + 12, sz, C_TEXT_DIM); }
    }
    ui_footer("A  Details    Left/Right  Category    X  Reload    B  Back");
}

/* ---- settings ---- */

enum { SET_AUTO, SET_TLS, SET_EXIT_ACTION, SET_RELOAD, SET_EXIT, SET_COUNT };

static int settings_update(u32 p) {
    ls_set.count = SET_COUNT; ls_set.rows = SET_COUNT;
    ui_list_nav(&ls_set, p);
    if (p & BTN_B) return SCR_MENU;
    if (p & (BTN_A | BTN_LEFT | BTN_RIGHT)) {
        switch (ls_set.sel) {
        case SET_AUTO: g_settings.auto_refresh = !g_settings.auto_refresh; break;
        case SET_TLS: g_settings.verify_tls = !g_settings.verify_tls; ui_toast("Takes effect after a restart", false); break;
        case SET_EXIT_ACTION: g_settings.exit_action = !g_settings.exit_action; break;
        case SET_RELOAD: if (p & BTN_A) store_request_refresh(); break;
        case SET_EXIT:
            if (p & BTN_A) {
                ui_confirm_open("Leave", g_settings.exit_action ? "Return to the loader (Swiss)?" : "Reboot the GameCube?", "Leave");
                pending_action = 4;
            }
            break;
        }
        if (ls_set.sel <= SET_EXIT_ACTION) {
            char path[160];
            fs_join(path, sizeof path, catalog_datadir(), "settings.ini");
            settings_save(path);
        }
    }
    return -1;
}

static void settings_draw(void) {
    ui_header("SETTINGS", online_text());
    ui_busy_banner();
    float x = UI_LEFT, w = UI_RIGHT - UI_LEFT;
    ui_panel(x, LIST_Y, w, ROW_H * SET_COUNT + 12);
    static const char *labels[SET_COUNT] = {
        "Load the catalogs when starting", "Check server certificates", "When leaving", "Reload the catalogs now", "Leave" };
    for (int i = 0; i < SET_COUNT; i++) {
        float y = LIST_Y + 6 + i * ROW_H;
        ui_row_highlight(x + 6, y, w - 12, ROW_H - 2, i == ls_set.sel);
        font_draw_shadow(x + 20, y + 10, FS_BODY, C_TEXT, labels[i]);
        const char *v = NULL;
        switch (i) {
        case SET_AUTO: v = g_settings.auto_refresh ? "On" : "Off"; break;
        case SET_TLS: v = g_settings.verify_tls ? "On" : "Off"; break;
        case SET_EXIT_ACTION: v = g_settings.exit_action ? "Return to loader" : "Reboot"; break;
        }
        if (v) ui_tag(x + w - 16, y + 12, v, ui_mix(ui_accent(), RGB(255, 255, 255), 0.3f));
    }
    float ay = LIST_Y + ROW_H * SET_COUNT + 30;
    char line[460];
    snprintf(line, sizeof line, "GC Homebrew Hub Browser %s", APP_VERSION);
    font_draw_shadow(x, ay, FS_SMALL, C_TEXT_DIM, line);
    snprintf(line, sizeof line, "Network: %s%s", store_net_up() ? net_ip_string() : "not connected",
             store_net_up() ? (store_tls_verified() ? "  (servers verified)" : "  (servers NOT verified)") : "");
    font_draw_shadow(x, ay + 20, FS_SMALL, C_TEXT_DIM, line);
    if (!store_net_up() && *store_net_error()) font_draw_clip(x, ay + 40, FS_SMALL, C_BAD, store_net_error(), w);
    snprintf(line, sizeof line, "Files: %s    Sources: %s", catalog_datadir(), g_settings.sources_url);
    font_draw_clip(x, ay + 60, FS_SMALL, C_TEXT_FAINT, line, w);
    ui_footer("A  Change    B  Back");
}

/* ---- dispatch ---- */

void screens_open(Screen s) {
    cur = s;
    enter_flash = 12;
    pending_remove = -1;
    if (s == SCR_INSTALLED) build_installed();
    if (s == SCR_STORE) build_store();
}

int screens_update(u32 p) {
    if (osk_active()) {
        int r = osk_update(p);
        if (r == 1) {
            char err[96];
            if (catalog_source_add(osk_text(), err, sizeof err)) { ui_toast("Source added", false); store_request_refresh(); }
            else ui_toast(err, true);
        }
        return -1;
    }
    if (ui_confirm_active()) {
        int r = ui_confirm_update(p);
        if (r == 1) {
            switch (pending_action) {
            case 1: catalog_source_remove(pending_index); ui_toast("Source removed", false); break;
            case 2: store_request_install(pending_index); break;
            case 3: store_request_remove(pending_index); cur = det_from; break;
            case 4: do_exit(); break;
            }
        }
        if (r) pending_action = 0;
        return -1;
    }
    switch (cur) {
    case SCR_SOURCES: return sources_update(p);
    case SCR_INSTALLED: return installed_update(p);
    case SCR_STORE: return storescr_update(p);
    case SCR_SETTINGS: return settings_update(p);
    case SCR_DETAILS: return details_update(p);
    default: return SCR_MENU;
    }
}

static Color section_accent(Screen s) {
    switch (s) {
    case SCR_SOURCES: return C_SOURCES;
    case SCR_INSTALLED: return C_INSTALLED;
    case SCR_SETTINGS: return C_SETTINGS;
    default: return C_STORE;
    }
}

void screens_draw(void) {
    ui_set_accent(section_accent(cur == SCR_DETAILS ? (Screen)det_from : cur));
    ui_background();
    switch (cur) {
    case SCR_SOURCES: sources_draw(); break;
    case SCR_INSTALLED: installed_draw(); break;
    case SCR_STORE: storescr_draw(); break;
    case SCR_SETTINGS: settings_draw(); break;
    case SCR_DETAILS: details_draw(); break;
    default: break;
    }
    if (enter_flash > 0) {
        gfx_rect(0, 0, SCREEN_W, SCREEN_H, ui_alpha(ui_mix(ui_accent(), RGB(255, 255, 255), 0.6f), enter_flash / 24.0f));
        enter_flash--;
    }
    ui_confirm_draw();
    osk_draw();
}
