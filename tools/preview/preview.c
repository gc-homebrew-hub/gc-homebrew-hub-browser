/* preview.c - renders the program's screens on a PC, for looking at the UI
 * without a GameCube. The real menu, screens and widgets run against the
 * software GX in gx_soft.c, a made-up card and catalog, and a script of
 * button presses; chosen frames are written as PPM images.
 *
 * Runs inside a folder with a directory literally named "sd:" (the card). */
#include "catalog.h"
#include "font.h"
#include "fsutil.h"
#include "gx_soft.h"
#include "input.h"
#include "net.h"
#include "osk.h"
#include "screens.h"
#include "settings.h"
#include "store.h"
#include "dol.h"
#include "ui.h"
#include "video.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the worker, network and launcher, faked ---- */

static bool busy;
static float progress = -1;
static const char *step_text = "", *job = "";

void store_init(void) {}
void store_poll(void) {}
StoreState store_state(void) { return busy ? STORE_INSTALLING : STORE_IDLE; }
bool store_busy(void) { return busy; }
const char *store_step(void) { return step_text; }
float store_progress(void) { return progress; }
const char *store_job_name(void) { return job; }
void store_request_refresh(void) {}
void store_request_install(int i, bool replace_root) { (void)i; (void)replace_root; }
int store_root_targets(int i, char *list, size_t cap) { (void)i; if (cap) list[0] = 0; return 0; }
void store_request_remove(int i) { (void)i; }
void store_cancel(void) {}
bool store_take_message(char *out, size_t cap, bool *error) { (void)out; (void)cap; (void)error; return false; }
bool store_net_up(void) { return true; }
const char *store_net_error(void) { return ""; }
bool store_tls_verified(void) { return true; }
const char *net_ip_string(void) { return "192.168.1.42"; }
bool dol_launch(const char *path, char *err, size_t errlen) { (void)path; snprintf(err, errlen, "preview"); return false; }

/* ---- the pad, scripted ---- */

static u32 pressed;
static float stick[4];
void input_init(void) {}
void input_poll(void) {}
u32 input_pressed(void) { return pressed; }
u32 input_held(void) { return 0; }
void input_sticks(float *x, float *y, float *cx, float *cy) { *x = stick[0]; *y = stick[1]; *cx = stick[2]; *cy = stick[3]; }

/* ---- a made-up catalog and card ---- */

static const char *index_json =
"{ \"format\": 1, \"name\": \"GC Homebrew Hub\", \"apps\": ["
"{\"slug\":\"swiss\",\"name\":\"Swiss\",\"author\":\"emu_kidid\",\"version\":\"0.6r1913\",\"category\":\"utilities\","
" \"short\":\"The Swiss army knife of GameCube homebrew\",\"long\":\"Boots games and homebrew from SD, network, IDE-EXI and more, with cheats, video mode forcing and a file manager.\",\"zip\":\"http://x/swiss.zip\",\"zip_size\":4194304},"
"{\"slug\":\"cubebrowser\",\"name\":\"CubeBrowser\",\"author\":\"Arial-Bold\",\"version\":\"1.4\",\"category\":\"utilities\","
" \"short\":\"A web browser for the GameCube\",\"long\":\"Browse simple web pages over the Broadband Adapter.\",\"zip\":\"http://x/cb.zip\",\"zip_size\":1887436},"
"{\"slug\":\"flashcube\",\"name\":\"Flashcube\",\"author\":\"lunzer\",\"version\":\"2.1\",\"category\":\"media\","
" \"short\":\"Flash cards with thumbnails\",\"long\":\"Study flash cards on the GameCube. Keeps its card sets in sd:/flashcube.\",\"zip\":\"http://x/fc.zip\",\"zip_size\":734003,\"copy_to_root\":[\"flashcube\"]},"
"{\"slug\":\"gc-mp3\",\"name\":\"GC-MP3\",\"author\":\"various\",\"version\":\"1.1\",\"category\":\"media\","
" \"short\":\"Plays MP3 files from the SD card\",\"long\":\"A music player.\",\"dol\":\"http://x/mp3.dol\",\"binary\":\"gc-mp3.dol\"},"
"{\"slug\":\"gbi\",\"name\":\"Game Boy Interface\",\"author\":\"Extrems\",\"version\":\"r1350\",\"category\":\"emulators\","
" \"short\":\"Game Boy Player software, rebuilt\",\"long\":\"Low-latency Game Boy Player front end.\",\"zip\":\"http://x/gbi.zip\",\"zip_size\":2097152},"
"{\"slug\":\"pong-cube\",\"name\":\"Pong Cube\",\"author\":\"devkitPro\",\"version\":\"1.0\",\"category\":\"games\","
" \"short\":\"Two paddles, one ball, four controllers\",\"long\":\"A tiny game.\",\"zip\":\"http://x/pong.zip\",\"zip_size\":102400},"
"{\"slug\":\"block-drop\",\"name\":\"Block Drop\",\"author\":\"cubedev\",\"version\":\"0.9\",\"category\":\"games\","
" \"short\":\"Falling blocks\",\"long\":\"Stack them.\",\"zip\":\"http://x/bd.zip\",\"zip_size\":310000},"
"{\"slug\":\"plasma\",\"name\":\"Plasma Demo\",\"author\":\"scene\",\"version\":\"1.0\",\"category\":\"demos\","
" \"short\":\"Old-school plasma and a chiptune\",\"long\":\"A demo.\",\"zip\":\"http://x/p.zip\",\"zip_size\":220000},"
"{\"slug\":\"neogeo-cd\",\"name\":\"Neo-Geo CD Redux\",\"author\":\"megalomaniac\",\"version\":\"1.0.3\",\"category\":\"emulators\","
" \"short\":\"Neo-Geo CD emulator\",\"long\":\"Plays Neo-Geo CD images.\",\"zip\":\"http://x/ng.zip\",\"zip_size\":1500000},"
"{\"slug\":\"memcard-pro\",\"name\":\"Memory Card Manager\",\"author\":\"suloku\",\"version\":\"1.4\",\"category\":\"utilities\","
" \"short\":\"Back up and restore saves\",\"long\":\"Copies memory card saves to and from the SD card.\",\"zip\":\"http://x/mc.zip\",\"zip_size\":420000}"
"]}";

static void make_card(void) {
    fs_mkdirs("sd:/apps/gc-homebrew-hub-browser");
    const char *loose[] = { "Snes9x", "Homebrew Demo" };
    for (int i = 0; i < 2; i++) {
        char p[160];
        snprintf(p, sizeof p, "sd:/apps/%s/boot.dol", loose[i]);
        char d[160];
        snprintf(d, sizeof d, "sd:/apps/%s", loose[i]);
        fs_mkdirs(d);
        fs_write_file(p, "dol", 3);
    }
    settings_defaults();
    catalog_set_card(true);
    catalog_init("sd:/apps/gc-homebrew-hub-browser");
    char err[96];
    catalog_source_add("https://raw.githubusercontent.com/gc-homebrew-hub/gc-homebrew-hub-catalog/main/index.json", err, sizeof err);
    catalog_source_add("http://192.168.1.20:8765/index.json", err, sizeof err);
    catalog_source_add("https://example.org/gc/index.json", err, sizeof err);
    Source *s = catalog_source(0);
    snprintf(s->name, sizeof s->name, "GC Homebrew Hub"); s->builtin = true; s->napps = 10;
    snprintf(s->status, sizeof s->status, "10 apps");
    s = catalog_source(1);
    snprintf(s->name, sizeof s->name, "Mac on the LAN"); s->napps = -1;
    snprintf(s->status, sizeof s->status, "Failed: could not connect (timed out)");
    s = catalog_source(2);
    snprintf(s->name, sizeof s->name, "example.org"); s->enabled = false;

    App *arr = malloc(sizeof(App) * MAX_APPS);
    int n = 0;
    char name[48];
    if (!catalog_parse_index(index_json, strlen(index_json), 0, arr, &n, MAX_APPS, name, sizeof name, err, sizeof err))
        fprintf(stderr, "index: %s\n", err);
    catalog_replace_apps(arr, n);
    catalog_installed_set("swiss", "Swiss", "0.6r1900", "swiss", "boot.dol");
    catalog_installed_set("cubebrowser", "CubeBrowser", "1.4", "cubebrowser", "boot.dol");
    catalog_installed_set("flashcube", "Flashcube", "2.0", "flashcube", "boot.dol");
    catalog_installed_set("gc-mp3", "GC-MP3", "1.1", "gc-mp3", "gc-mp3.dol");
    catalog_installed_set("gbi", "Game Boy Interface", "r1350", "gbi", "boot.dol");
    catalog_installed_set("plasma", "Plasma Demo", "1.0", "plasma", "boot.dol");
    catalog_installed_set("memcard-pro", "Memory Card Manager", "1.4", "memcard-pro", "boot.dol");
    catalog_scan_card();
    catalog_recompute();
}

/* ---- the frame loop of main.c, driven by the script ---- */

static void frame(u32 p, bool draw) {
    pressed = p;
    if (menu_focused() && !screens_modal()) {
        int next = menu_update(p);
        if (next >= 0) screens_open((Screen)next);
    } else {
        int next = screens_update(p);
        if (next == SCR_DOCK) menu_focus();
        else if (next >= 0) screens_open((Screen)next);
    }
    soft_enabled = draw;
    video_frame_begin();
    ui_backdrop();
    screens_draw(!menu_focused());
    menu_draw();
    screens_draw_overlays();
    ui_toasts_update_draw();
    video_frame_end();
}

static void press(u32 p) { frame(p, false); }
static void idle(int n) { while (n-- > 0) frame(0, false); }

static void shot(const char *name) {
    frame(0, true);
    char path[160];
    snprintf(path, sizeof path, "%s.ppm", name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", SOFT_W, SOFT_H);
    for (int y = 0; y < SOFT_H; y++)
        for (int x = 0; x < SOFT_W; x++)
            for (int c = 0; c < 3; c++) {
                float v = soft_shown[y][x][c];
                fputc(v <= 0 ? 0 : v >= 1 ? 255 : (int)(v * 255 + 0.5f), f);
            }
    fclose(f);
    printf("%s\n", path);
}

/* opens a section from the bar along the bottom: B into the bar, over, A */
static void section(int steps_right) {
    press(BTN_B);
    for (int i = 0; i < steps_right; i++) press(BTN_RIGHT);
    for (int i = 0; i < -steps_right; i++) press(BTN_LEFT);
    press(BTN_A);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    video_init();
    font_init();
    make_card();
    screens_open(SCR_STORE);

    idle(10);                                  shot("01-homebrew-hub");
    press(BTN_RIGHT); press(BTN_RIGHT); idle(4); shot("02-hub-emulators");
    press(BTN_LEFT); press(BTN_LEFT); press(BTN_DOWN); press(BTN_DOWN);
    press(BTN_A); idle(4);                     shot("03-details");
    busy = true; step_text = "Downloading"; job = "Flashcube"; progress = 0.62f;
    idle(2);                                   shot("04-details-installing");
    busy = false;
    press(BTN_B); press(BTN_B); press(BTN_RIGHT); idle(30); shot("05-section-bar");
    press(BTN_A); idle(4);                     shot("06-installed");
    press(BTN_Y); idle(4);                     shot("07-installed-updates");
    press(BTN_A); idle(4);                     shot("08-confirm");
    press(BTN_B); press(BTN_Y);
    section(1); idle(4);                       shot("09-sources");
    press(BTN_X); idle(4); press(BTN_RIGHT); press(BTN_RIGHT); idle(10); shot("10-keyboard");
    press(BTN_START); idle(4);
    section(1); idle(4);                       shot("11-settings");
    ui_toast("Flashcube 2.1 installed", false);
    ui_toast("Mac on the LAN: could not connect (timed out)", true);
    idle(10);                                  shot("12-toasts");
    /* browsing without a card (as in Dolphin) */
    idle(320); catalog_set_card(false);
    section(-2); idle(4);                      shot("13-no-card");
    return 0;
}
