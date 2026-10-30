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
    bool have_font = font_init();
    bool have_card = mount_card();
    char path[160];
    if (have_card) {
        fs_mkdirs(DATA_DIR);
        fs_join(path, sizeof path, DATA_DIR, "settings.ini");
        settings_load(path);
    } else settings_defaults();
    catalog_init(DATA_DIR);
    store_init();

    screens_open(SCR_STORE);
    int retry_frames = 0;
    while (SYS_MainLoop()) {
        input_poll();
        store_poll();
        u32 p = input_pressed();

        if (!have_card) {
            if ((p & BTN_A) || ++retry_frames % 120 == 0) {
                have_card = mount_card();
                if (have_card) {
                    fs_mkdirs(DATA_DIR);
                    fs_join(path, sizeof path, DATA_DIR, "settings.ini");
                    settings_load(path);
                    catalog_init(DATA_DIR);
                    ui_toast("SD card found", false);
                }
            }
        } else if (menu_focused() && !screens_modal()) {
            int next = menu_update(p);
            if (next >= 0) screens_open((Screen)next);
        } else {
            int next = screens_update(p);
            if (next == SCR_DOCK) menu_focus();
            else if (next >= 0) screens_open((Screen)next);
        }

        video_frame_begin();
        ui_backdrop();
        if (!have_card) {
            menu_draw(false);
            if (have_font) {
                float w = 440, h = 120, x = (SCREEN_W - w) / 2, y = 190;
                ui_panel(x, y, w, h);
                ui_desc_bar(x + 12, y + 12, w - 24, 28);
                font_draw(x + 22, y + 12 + (28 - font_height(FS_BODY)) / 2, FS_BODY, C_TEXT, "No SD card found");
                font_draw(x + 22, y + 54, FS_SMALL, C_TEXT_DIM, "Insert a card (SD2SP2, or an SD adapter in slot A or B).");
                ui_hints(x + 22, y + 82, "A  Try again");
            }
        } else {
            screens_draw(!menu_focused());
            menu_draw(true);
            screens_draw_overlays();
        }
        ui_toasts_update_draw();
        video_frame_end();
    }
    return 0;
}
