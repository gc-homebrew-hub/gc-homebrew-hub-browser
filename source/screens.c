/* screens.c - Sources, Installed, App store, Settings and the app details page. */
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

#define ROW_H 46
#define LIST_Y 104
#define LIST_ROWS 7

static Screen cur;
static ListState ls_src, ls_inst, ls_store, ls_set;
static bool updates_only;
static int store_cat;
static int pending_remove = -1;
static int pending_action;            /* what a confirm answers: 1 remove source, 2 update, 3 remove app, 4 exit */
static int pending_index;

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
    char right[64] = "";
    if (a) snprintf(right, sizeof right, "Version %s", a->version);
    ui_header(name, right);
    float x = UI_LEFT, y = 68, w = UI_RIGHT - UI_LEFT;
    ui_panel(x, y, w, 300);
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
        int n = font_draw_wrapped(lx, ty, FS_BODY, C_TEXT, desc, w - 36, 7, true);
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
            Color c = det_actions[i] == ACT_REMOVE ? RGBA(255, 120, 110, hot ? 200 : 60)
                    : det_actions[i] == ACT_UPDATE ? RGBA(255, 214, 90, hot ? 220 : 70)
                    : RGBA(255, 255, 255, hot ? 190 : 45);
            gfx_round_rect(bx, by, bw, 38, 10, c);
            if (hot) gfx_frame(bx, by, bw, 38, 2, RGBA(255, 255, 255, 200));
            font_draw_shadow(bx + 18, by + 8, FS_BODY, hot ? RGB(30, 20, 60) : C_TEXT, lab);
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
        font_draw_clip(x + 20, y + 4, FS_BODY, nc, s->name, 330);
        font_draw_clip(x + 20, y + 4 + font_height(FS_BODY) - 2, FS_SMALL, C_TEXT_FAINT, s->url, 400);
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
    ui_list_nav(&ls_inst, p);
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
    float x = UI_LEFT, w = UI_RIGHT - UI_LEFT;
    ui_panel(x, LIST_Y, w, ROW_H * LIST_ROWS + 12);
    if (!inst_nitems)
        font_draw_center(SCREEN_W / 2, LIST_Y + 60, FS_BODY, C_TEXT_DIM,
                         updates_only ? "Everything is up to date." : "No apps in sd:/apps yet. Visit the App store.");
    for (int r = 0; r < LIST_ROWS && ls_inst.top + r < inst_nitems; r++) {
        int i = inst_items[ls_inst.top + r];
        Installed *in = catalog_installed(i);
        App *a = in->app >= 0 ? catalog_app(in->app) : NULL;
        float y = LIST_Y + 6 + r * ROW_H;
        ui_row_highlight(x + 6, y, w - 12, ROW_H - 2, ls_inst.top + r == ls_inst.sel);
        font_draw_clip(x + 20, y + 4, FS_BODY, C_TEXT, in->name, 360);
        char sub[96];
        if (in->managed && *in->version) snprintf(sub, sizeof sub, "Version %s   sd:/apps/%s", in->version, in->folder);
        else snprintf(sub, sizeof sub, "sd:/apps/%s/%s", in->folder, in->binary);
        font_draw_clip(x + 20, y + 4 + font_height(FS_BODY) - 2, FS_SMALL, C_TEXT_FAINT, sub, 400);
        if (a && a->update) { char t[40]; snprintf(t, sizeof t, "Update %s", a->version); ui_tag(x + w - 16, y + 12, t, C_ACCENT); }
        else if (a) ui_tag(x + w - 16, y + 12, "Up to date", C_GOOD);
        else ui_tag(x + w - 16, y + 12, "Not in a catalog", C_TEXT_FAINT);
    }
    ui_footer(updates_only ? "A  Update    Y  Show all    X  Reload    B  Back"
                           : "A  Details    Y  Updates only    X  Reload    B  Back");
}

/* ---- app store ---- */

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
    ui_header("APP STORE", online_text());
    ui_busy_banner();
    float x = UI_LEFT, w = UI_RIGHT - UI_LEFT;
    /* category strip (the busy banner takes its place while the worker runs) */
    float cx = x;
    for (int i = 0; i < NCATS && !store_busy(); i++) {
        float cw = font_width(cat_names[i], FS_SMALL) + 18;
        bool hot = i == store_cat;
        gfx_round_rect(cx, 70, cw, 24, 8, hot ? C_TILE_HOT : C_TILE);
        font_draw_shadow(cx + 9, 72, FS_SMALL, hot ? RGB(30, 20, 60) : C_TEXT, cat_names[i]);
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
        font_draw_clip(x + 20, y + 4, FS_BODY, C_TEXT, a->name, 360);
        char sub[160];
        snprintf(sub, sizeof sub, "%s   %s", *a->author ? a->author : "", a->summary);
        font_draw_clip(x + 20, y + 4 + font_height(FS_BODY) - 2, FS_SMALL, C_TEXT_FAINT, sub, 420);
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
        if (v) ui_tag(x + w - 16, y + 12, v, C_ACCENT);
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

void screens_draw(void) {
    switch (cur) {
    case SCR_SOURCES: sources_draw(); break;
    case SCR_INSTALLED: installed_draw(); break;
    case SCR_STORE: storescr_draw(); break;
    case SCR_SETTINGS: settings_draw(); break;
    case SCR_DETAILS: details_draw(); break;
    default: break;
    }
    ui_confirm_draw();
    osk_draw();
}
