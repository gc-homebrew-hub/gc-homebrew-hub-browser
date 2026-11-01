/* main.c - start-up and the frame loop. */
#include "catalog.h"
#include "font.h"
#include "fsutil.h"
#include "input.h"
#include "log.h"
#include "memstat.h"
#include "screens.h"
#include "settings.h"
#include "store.h"
#include "ui.h"
#include "video.h"
#include <fat.h>
#include <gccore.h>
#include <sdcard/gcsd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define DATA_DIR "sd:/apps/gc-homebrew-hub-browser"

/* Starts the log on the card, with what is worth knowing about this start */
static void start_log(void) {
    char path[160];
    fs_join(path, sizeof path, DATA_DIR, "log.txt");
    log_open(path);
    time_t now = time(NULL);
    char when[32];
    strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", localtime(&now));
    log_printf("=== GC Homebrew Hub Browser %s, console clock %s, %s video, %zu KB free",
               APP_VERSION, when, VIDEO_GetCurrentTvMode() == VI_PAL ? "PAL" : "NTSC", mem_free_total() >> 10);
}

static bool mount_card(void) {
    DISC_INTERFACE *ifaces[] = { &__io_gcsd2, &__io_gcsda, &__io_gcsdb };
    for (int i = 0; i < 3; i++) {
        if (ifaces[i]->startup(ifaces[i]) && ifaces[i]->isInserted(ifaces[i]) && fatMountSimple("sd", ifaces[i])) return true;
        if (ifaces[i]->shutdown) ifaces[i]->shutdown(ifaces[i]);
    }
    return false;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    video_init();
    input_init();
    font_init();
    bool have_card = mount_card();
    char path[160];
    if (have_card) {
        fs_mkdirs(DATA_DIR);
        start_log();
        fs_join(path, sizeof path, DATA_DIR, "settings.ini");
        settings_load(path);
    } else settings_defaults();
    catalog_set_card(have_card);
    catalog_init(DATA_DIR);
    store_init();
    if (!have_card) ui_toast("No SD card: you can browse, but not install", true);

    screens_open(SCR_STORE);
    int retry_frames = 0;
    while (SYS_MainLoop()) {
        input_poll();
        store_poll();
        u32 p = input_pressed();

        /* look for a card now and then while there is none */
        if (!have_card && !store_busy() && ++retry_frames % 120 == 0) {
            have_card = mount_card();
            if (have_card) {
                fs_mkdirs(DATA_DIR);
                start_log();
                fs_join(path, sizeof path, DATA_DIR, "settings.ini");
                settings_load(path);
                catalog_set_card(true);
                catalog_init(DATA_DIR);
                ui_toast("SD card found", false);
                store_request_refresh();
            }
        }
        if (menu_focused() && !screens_modal()) {
            int next = menu_update(p);
            if (next >= 0) screens_open((Screen)next);
        } else {
            int next = screens_update(p);
            if (next == SCR_DOCK) menu_focus();
            else if (next >= 0) screens_open((Screen)next);
        }

        video_frame_begin();
        ui_backdrop();
        screens_draw(!menu_focused());
        menu_draw();
        screens_draw_overlays();
        ui_toasts_update_draw();
        video_frame_end();
    }
    return 0;
}
