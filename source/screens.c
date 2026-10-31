/* screens.c - Homebrew Hub, Installed, Sources, Settings and the app details
 * page. Each is a side pane on the left (context and the buttons that work
 * here) and a list of frosted rows on the right, with the description bar
 * telling more about the selected row. */
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static Screen cur = SCR_STORE;
static bool focus = true;             /* the list has the controls (not the bar of sections) */
static ListState ls_src, ls_inst, ls_store, ls_set;
static bool updates_only;
static int store_cat;
static int pending_action;            /* what a confirm answers: 1 remove source, 2 update, 3 remove app, 4 exit */
static int pending_index;

/* details */
static int det_app, det_inst, det_btn;
static Screen det_from;
enum { ACT_INSTALL, ACT_UPDATE, ACT_START, ACT_REMOVE };
static int det_actions[4], det_nact;

static const char *cat_keys[] = { "", "games", "emulators", "media", "utilities", "demos" };
static const char *cat_names[] = { "All", "Games", "Emulators", "Media", "Utilities", "Demos" };
#define NCATS 6

static void save_settings(void) {
    char path[160];
    fs_join(path, sizeof path, catalog_datadir(), "settings.ini");
    settings_save(path);
}

static void do_exit(void) {
    save_settings();
    if (g_settings.exit_action == 0) SYS_ResetSystem(SYS_HOTRESET, 0, 0);
    exit(0);
}

static void launch(const Installed *in) {
    if (!in) return;
    char path[300], err[128];
    snprintf(path, sizeof path, APPS_DIR "/%s/%s", in->folder, in->binary);
    save_settings();
    if (!dol_launch(path, err, sizeof err)) {
        char m[200];
        snprintf(m, sizeof m, "%s did not start: %s", in->name, err);
        ui_toast(m, true);
    }
}

/* A row standing in for an empty list */
static void empty_list(const char *text) {
    float x, y;
    ui_list_row(0, false, false, &x, &y);
    font_draw_center(x + ROW_W / 2, y + (ROW_H - font_height(FS_SMALL)) / 2, FS_SMALL, C_TEXT_DIM, text);
}

/* Name over a second line, right of a plate (or of nothing when px == 0) */
static void row_text(float x, float y, float tx, const char *name, const char *sub, float maxw) {
    float bh = font_height(FS_BODY), sh = font_height(FS_SMALL), top = y + (ROW_H - bh - sh) / 2;
    font_draw_clip(x + tx, top, FS_BODY, C_TEXT, name, maxw);
    font_draw_clip(x + tx, top + bh, FS_SMALL, C_TEXT_DIM, sub, maxw);
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
    ls_inst.rows = LIST_ROWS;
    ui_list_clamp(&ls_inst);
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

static void open_details(int app, int inst, Screen from) {
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
            if (!store_net_up()) ui_toast("No network connection", true);
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

    /* the side pane: a plate and the facts */
    float y = ui_side_pane(a ? "App" : "On the card");
    ui_plate(SIDE_X + 12, y, SIDE_W - 24, 44, name);
    y += 44 + 10;
    char v[64];
    if (a) {
        ui_side_line(&y, "Version", a->version, C_TEXT);
        ui_side_line(&y, "Author", *a->author ? a->author : "unknown", C_TEXT);
        if (a->size) ui_format_size(a->size, v, sizeof v); else snprintf(v, sizeof v, "unknown");
        ui_side_line(&y, "Size", v, C_TEXT);
        Source *s = catalog_source(a->source);
        ui_side_line(&y, "From", s ? s->name : "?", C_TEXT);
    } else {
        ui_side_line(&y, "Folder", in->folder, C_TEXT);
        ui_side_line(&y, "Program", in->binary, C_TEXT);
    }
    ui_side_hints("D-pad  Choose    A  Do it    B  Back");

    /* the main panel */
    float x = MAIN_X, w = MAIN_W, py = BODY_Y, ph = BODY_BOTTOM - BODY_Y, lx = x + 16, tw = w - 32;
    ui_panel(x, py, w, ph);
    float ty = py + 12;
    font_draw_clip(lx, ty, FS_BIG, C_TEXT, name, tw);
    ty += font_height(FS_BIG) + 2;
    char line[200];
    if (in) {
        if (a && a->update) snprintf(line, sizeof line, "Update available: %s to %s", *in->version ? in->version : "?", a->version);
        else if (in->managed && *in->version) snprintf(line, sizeof line, "Installed: version %s in sd:/apps/%s", in->version, in->folder);
        else snprintf(line, sizeof line, "On the card in sd:/apps/%s", in->folder);
        font_draw_clip(lx, ty, FS_SMALL, a && a->update ? C_GOLD : C_GOOD, line, tw);
    } else font_draw(lx, ty, FS_SMALL, C_TEXT_DIM, "Not installed");
    ty += font_height(FS_SMALL) + 8;
    gfx_rect(lx, ty, tw, 1, RGBA(255, 255, 255, 40));
    ty += 10;
    if (a) {
        const char *desc = *a->description ? a->description : a->summary;
        int n = font_draw_wrapped(lx, ty, FS_BODY, C_TEXT, desc, tw, 6, true);
        ty += n * font_line_pitch(FS_BODY) + 8;
        if (a->nto_root) {
            snprintf(line, sizeof line, "Also puts %s at the root of the card.", a->to_root[0]);
            font_draw_clip(lx, ty, FS_SMALL, C_TEXT_DIM, line, tw); ty += font_height(FS_SMALL) + 4;
        }
        if (*a->homepage) font_draw_clip(lx, ty, FS_SMALL, C_TEXT_FAINT, a->homepage, tw);
    } else {
        font_draw_wrapped(lx, ty, FS_BODY, C_TEXT_DIM,
            "This app was not installed from a catalog, so there is nothing to update. You can start it or remove its folder.",
            tw, 4, true);
    }

    /* the actions along the bottom of the panel */
    details_actions();
    float bh = 34, by = py + ph - bh - 14;
    if (store_busy()) {
        snprintf(line, sizeof line, "%s...", store_step());
        font_draw(lx, by + (bh - font_height(FS_BODY)) / 2, FS_BODY, C_TEXT, line);
        ui_progress(lx + 170, by + 12, tw - 170, 10, store_progress());
        return;
    }
    float bx = lx;
    for (int i = 0; i < det_nact; i++) {
        const char *lab = action_label(det_actions[i]);
        float bw = font_width(lab, FS_BODY) + 36;
        if (bw < 96) bw = 96;
        ui_button(bx, by, bw, bh, false);
        if (i == det_btn && focus) ui_ring(bx, by, bw, bh, true);
        Color c = det_actions[i] == ACT_REMOVE ? C_BAD : det_actions[i] == ACT_UPDATE ? C_GOLD : C_TEXT;
        font_draw_center(bx + bw / 2, by + (bh - font_height(FS_BODY)) / 2, FS_BODY, c, lab);
        bx += bw + 16;
    }
}

/* ---- homebrew hub (the store) ---- */

static int storescr_update(u32 p) {
    build_store();
    ui_list_nav(&ls_store, p);
    if (p & BTN_B) return SCR_DOCK;
    if (p & BTN_X) store_request_refresh();
    if (p & BTN_RIGHT) { store_cat = (store_cat + 1) % NCATS; ls_store.sel = 0; ls_store.top = 0; build_store(); }
    if (p & BTN_LEFT) { store_cat = (store_cat + NCATS - 1) % NCATS; ls_store.sel = 0; ls_store.top = 0; build_store(); }
    if ((p & BTN_A) && store_nitems) open_details(store_items[ls_store.sel], -1, SCR_STORE);
    return -1;
}

static void storescr_draw(void) {
    build_store();
    float y = ui_side_pane("Categories");
    for (int c = 0; c < NCATS; c++, y += 24) {
        int n = 0;
        for (int i = 0; i < catalog_app_count(); i++)
            if (!c || !strcasecmp(catalog_app(i)->category, cat_keys[c])) n++;
        bool hot = c == store_cat;
        if (hot) {
            gfx_rect_hgradient(SIDE_X + 8, y - 3, SIDE_W - 16, font_height(FS_SMALL) + 6, RGBA(232, 207, 74, 70), RGBA(232, 207, 74, 0));
            gfx_rect(SIDE_X + 8, y - 3, 3, font_height(FS_SMALL) + 6, C_GOLD);
        }
        font_draw(SIDE_X + 18, y, FS_SMALL, hot ? C_TEXT : C_TEXT_DIM, cat_names[c]);
        char s[8];
        snprintf(s, sizeof s, "%d", n);
        font_draw_right(SIDE_X + SIDE_W - 14, y, FS_SMALL, hot ? C_GOLD : C_TEXT_FAINT, s);
    }
    ui_side_hints("A  Details    D-pad  Category    X  Reload    B  Sections");

    if (!store_nitems) {
        empty_list(catalog_app_count() ? "Nothing in this category." :
                   store_net_up() ? "No apps loaded. Press X to reload the catalogs." :
                   "Offline and no saved catalog.");
        ui_desc(NULL, NULL);
        return;
    }
    for (int r = 0; r < LIST_ROWS && ls_store.top + r < store_nitems; r++) {
        App *a = catalog_app(store_items[ls_store.top + r]);
        float x, ry;
        ui_list_row(r, ls_store.top + r == ls_store.sel, focus, &x, &ry);
        ui_plate(x + 6, ry + 5, 44, ROW_H - 10, a->name);
        char sub[160];
        snprintf(sub, sizeof sub, "%s  -  %s", *a->author ? a->author : "unknown", *a->category ? a->category : "app");
        row_text(x, ry, 60, a->name, sub, ROW_W - 170);
        char t[40];
        if (a->update) { snprintf(t, sizeof t, "Update %s", a->version); ui_row_status(ry, t, C_GOLD); }
        else if (a->installed >= 0) ui_row_status(ry, "Installed", C_GOOD);
        else { if (a->size) ui_format_size(a->size, t, sizeof t); else snprintf(t, sizeof t, "v%s", a->version); ui_row_status(ry, t, C_TEXT_DIM); }
    }
    ui_scrollbar(MAIN_X + MAIN_W - 8, BODY_Y + 3, LIST_ROWS * ROW_PITCH - 6, &ls_store);
    App *a = catalog_app(store_items[ls_store.sel]);
    char l2[80];
    snprintf(l2, sizeof l2, "Version %s    %d of %d", a->version, ls_store.sel + 1, store_nitems);
    ui_desc(a->summary, l2);
}

/* ---- installed ---- */

static int installed_update(u32 p) {
    build_installed();
    ui_list_nav(&ls_inst, p);
    if (p & BTN_B) return SCR_DOCK;
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
    float y = ui_side_pane("SD card");
    char v[32];
    snprintf(v, sizeof v, "%d", catalog_installed_count());
    ui_side_line(&y, "Apps in sd:/apps", v, C_TEXT);
    int up = catalog_update_count();
    snprintf(v, sizeof v, "%d", up);
    ui_side_line(&y, "Updates", v, up ? C_GOLD : C_TEXT);
    ui_side_line(&y, "Showing", updates_only ? "Updates only" : "All apps", C_TEXT);
    ui_side_hints(updates_only ? "A  Update    Y  Show all    X  Reload    B  Sections"
                               : "A  Details    Y  Updates only    X  Reload    B  Sections");

    if (!inst_nitems) {
        empty_list(updates_only ? "Everything is up to date." : "No apps in sd:/apps yet. Visit the Hub.");
        ui_desc(NULL, NULL);
        return;
    }
    for (int r = 0; r < LIST_ROWS && ls_inst.top + r < inst_nitems; r++) {
        Installed *in = catalog_installed(inst_items[ls_inst.top + r]);
        App *a = in->app >= 0 ? catalog_app(in->app) : NULL;
        float x, ry;
        ui_list_row(r, ls_inst.top + r == ls_inst.sel, focus, &x, &ry);
        ui_plate(x + 6, ry + 5, 44, ROW_H - 10, in->name);
        char sub[112];
        if (in->managed && *in->version) snprintf(sub, sizeof sub, "Version %s  -  sd:/apps/%s", in->version, in->folder);
        else snprintf(sub, sizeof sub, "sd:/apps/%s/%s", in->folder, in->binary);
        row_text(x, ry, 60, in->name, sub, ROW_W - 170);
        char t[40];
        if (a && a->update) { snprintf(t, sizeof t, "Update %s", a->version); ui_row_status(ry, t, C_GOLD); }
        else if (a) ui_row_status(ry, "Up to date", C_GOOD);
        else ui_row_status(ry, "Not in a catalog", C_TEXT_FAINT);
    }
    ui_scrollbar(MAIN_X + MAIN_W - 8, BODY_Y + 3, LIST_ROWS * ROW_PITCH - 6, &ls_inst);
    Installed *in = catalog_installed(inst_items[ls_inst.sel]);
    App *a = in->app >= 0 ? catalog_app(in->app) : NULL;
    char l2[200];
    snprintf(l2, sizeof l2, "sd:/apps/%s/%s    %d of %d", in->folder, in->binary, ls_inst.sel + 1, inst_nitems);
    ui_desc(a && *a->summary ? a->summary : "Not installed from a catalog", l2);
}

/* ---- sources ---- */

static int sources_update(u32 p) {
    ls_src.count = catalog_source_count(); ls_src.rows = LIST_ROWS;
    ui_list_nav(&ls_src, p);
    if (p & BTN_B) return SCR_DOCK;
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
    int n = catalog_source_count(), on = 0;
    for (int i = 0; i < n; i++) if (catalog_source(i)->enabled) on++;
    float y = ui_side_pane("Network");
    bool up = store_net_up();
    ui_side_line(&y, "Status", up ? "Online" : "Offline", up ? C_GOOD : C_BAD);
    if (up) ui_side_line(&y, "Address", net_ip_string(), C_TEXT);
    else if (*store_net_error()) ui_side_line(&y, "Error", store_net_error(), C_BAD);
    if (up) ui_side_line(&y, "Servers", store_tls_verified() ? "Verified" : "NOT verified", store_tls_verified() ? C_TEXT : C_BAD);
    char v[32];
    snprintf(v, sizeof v, "%d, %d on", n, on);
    ui_side_line(&y, "Sources", v, C_TEXT);
    ui_side_hints("A  On/Off    X  Add    Y  Remove    Start  Reload    B  Sections");

    ls_src.count = n; ls_src.rows = LIST_ROWS;
    ui_list_clamp(&ls_src);
    if (!n) {
        empty_list("No sources yet. Press X to add one.");
        ui_desc(NULL, NULL);
        return;
    }
    for (int r = 0; r < LIST_ROWS && ls_src.top + r < n; r++) {
        Source *s = catalog_source(ls_src.top + r);
        float x, ry;
        ui_list_row(r, ls_src.top + r == ls_src.sel, focus, &x, &ry);
        Color lamp = !s->enabled ? RGB(96, 104, 118) : s->napps < 0 ? C_BAD : s->napps > 0 ? C_GOOD : C_GOLD;
        gfx_rect(x + 14, ry + ROW_H / 2 - 4, 8, 8, lamp);
        ui_outline(x + 13, ry + ROW_H / 2 - 5, 10, 10, 0, 1, RGBA(0, 0, 0, 160));
        row_text(x, ry, 34, s->name, s->url, ROW_W - 150);
        if (!s->enabled) ui_row_status(ry, "Off", C_TEXT_FAINT);
        else if (s->napps < 0) ui_row_status(ry, "Failed", C_BAD);
        else ui_row_status(ry, s->status, s->napps > 0 ? C_GOOD : C_TEXT_DIM);
    }
    ui_scrollbar(MAIN_X + MAIN_W - 8, BODY_Y + 3, LIST_ROWS * ROW_PITCH - 6, &ls_src);
    Source *s = catalog_source(ls_src.sel);
    ui_desc(s->url, s->napps < 0 ? s->status : s->builtin ? "Built in: can be switched off, not removed" : s->status);
}

/* ---- settings ---- */

enum { SET_AUTO, SET_TLS, SET_EXIT_ACTION, SET_RELOAD, SET_EXIT, SET_COUNT };

static int settings_update(u32 p) {
    ls_set.count = SET_COUNT; ls_set.rows = SET_COUNT;
    ui_list_nav(&ls_set, p);
    if (p & BTN_B) return SCR_DOCK;
    if (p & (BTN_A | BTN_LEFT | BTN_RIGHT)) {
        switch (ls_set.sel) {
        case SET_AUTO: g_settings.auto_refresh = !g_settings.auto_refresh; break;
        case SET_TLS: g_settings.verify_tls = !g_settings.verify_tls; ui_toast("Takes effect after a restart", false); break;
        case SET_EXIT_ACTION: g_settings.exit_action = !g_settings.exit_action; break;
        case SET_RELOAD: if (p & BTN_A) store_request_refresh(); break;
        case SET_EXIT: if (p & BTN_A) screens_ask_exit(); break;
        }
        if (ls_set.sel <= SET_EXIT_ACTION) save_settings();
    }
    return -1;
}

static void settings_draw(void) {
    float y = ui_side_pane("About");
    ui_side_line(&y, "Version", APP_VERSION, C_TEXT);
    ui_side_line(&y, "Network", store_net_up() ? net_ip_string() : "not connected", store_net_up() ? C_TEXT : C_BAD);
    ui_side_line(&y, "Files", catalog_datadir(), C_TEXT);
    ui_side_hints("A  Change    B  Sections");

    static const char *labels[SET_COUNT] = {
        "Load the catalogs when starting", "Check server certificates", "When leaving", "Reload the catalogs now", "Leave" };
    static const char *help[SET_COUNT] = {
        "Fetch every source's index each time the program starts.",
        "Check HTTPS servers against the certificate bundle. Takes effect after a restart.",
        "What Exit does: reboot the GameCube, or return to the loader (Swiss).",
        "Fetch every source's index now.",
        "Save the settings and leave the program.",
    };
    for (int i = 0; i < SET_COUNT; i++) {
        float x, ry;
        ui_list_row(i, i == ls_set.sel, focus, &x, &ry);
        font_draw(x + 14, ry + (ROW_H - font_height(FS_BODY)) / 2, FS_BODY, C_TEXT, labels[i]);
        const char *v = NULL;
        switch (i) {
        case SET_AUTO: v = g_settings.auto_refresh ? "On" : "Off"; break;
        case SET_TLS: v = g_settings.verify_tls ? "On" : "Off"; break;
        case SET_EXIT_ACTION: v = g_settings.exit_action ? "Return to loader" : "Reboot"; break;
        }
        if (v) ui_row_status(ry, v, C_GOLD);
    }
    ui_desc(help[ls_set.sel], ls_set.sel == SET_RELOAD ? g_settings.sources_url : NULL);
}

/* ---- dispatch ---- */

void screens_open(Screen s) {
    cur = s;
    if (s == SCR_INSTALLED) build_installed();
    if (s == SCR_STORE) build_store();
}

Screen screens_section(void) { return cur == SCR_DETAILS ? det_from : cur; }

bool screens_modal(void) { return osk_active() || ui_confirm_active(); }

void screens_ask_exit(void) {
    ui_confirm_open("Leave", g_settings.exit_action ? "Return to the loader (Swiss)?" : "Reboot the GameCube?", "Leave");
    pending_action = 4;
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
    default: return -1;
    }
}

void screens_draw(bool f) {
    focus = f;
    switch (cur) {
    case SCR_SOURCES: sources_draw(); break;
    case SCR_INSTALLED: installed_draw(); break;
    case SCR_STORE: storescr_draw(); break;
    case SCR_SETTINGS: settings_draw(); break;
    case SCR_DETAILS: details_draw(); break;
    default: break;
    }
}

void screens_draw_overlays(void) {
    ui_confirm_draw();
    osk_draw();
}
