/* main.c - start-up and the frame loop. */
#include "catalog.h"
#include "font.h"
#include "fsutil.h"
#include "input.h"
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

#define DATA_DIR "sd:/apps/gc-homebrew-hub-browser"

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
        fs_join(path, sizeof path, DATA_DIR, "settings.ini");
        settings_load(path);
    } else settings_defaults();
    catalog_set_card(have_card);
    catalog_init(DATA_DIR);
    store_init();
    if (!have_card) ui_toast("No SD card: you can browse, but not install", true);

    Screen screen = SCR_MENU;
    menu_reset();
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
                fs_join(path, sizeof path, DATA_DIR, "settings.ini");
                settings_load(path);
                catalog_set_card(true);
                catalog_init(DATA_DIR);
                ui_toast("SD card found", false);
                store_request_refresh();
            }
        }
        if (screen == SCR_MENU) {
            int next = menu_update(p);
            if (next >= 0) { screen = (Screen)next; screens_open(screen); }
        } else {
            int next = screens_update(p);
            if (next == SCR_MENU) { screen = SCR_MENU; menu_reset(); }
            else if (next >= 0) { screen = (Screen)next; screens_open(screen); }
        }

        video_frame_begin();
        if (screen == SCR_MENU) menu_draw();
        else screens_draw();
        ui_toasts_update_draw();
        video_frame_end();
    }
    return 0;
}
