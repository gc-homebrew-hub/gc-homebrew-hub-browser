/* main.c - start-up and the frame loop. */
#include "catalog.h"
#include "emusd.h"
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
    DISC_INTERFACE *ifaces[] = {
#ifdef EMU_SD
        &__io_emusd,   /* the card in memory, for testing in an emulator */
#endif
        &__io_gcsd2, &__io_gcsda, &__io_gcsdb
    };
    for (int i = 0; i < (int)(sizeof ifaces / sizeof *ifaces); i++) {
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

    Screen screen = SCR_MENU;
    menu_reset();
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
        } else if (screen == SCR_MENU) {
            int next = menu_update(p);
            if (next >= 0) { screen = (Screen)next; screens_open(screen); }
        } else {
            int next = screens_update(p);
            if (next == SCR_MENU) { screen = SCR_MENU; menu_reset(); }
            else if (next >= 0) { screen = (Screen)next; screens_open(screen); }
        }

        video_frame_begin();
        if (!have_card) {
            ui_menu_background();
            gfx_cube(320, 200, 50, 22, video_frame_count() * 0.5f, 0, C_CUBE, 1.0f);
            if (have_font) {
                font_draw_center(320, 290, FS_BIG, C_TEXT, "No SD card found");
                font_draw_center(320, 330, FS_BODY, C_TEXT_DIM, "Insert a card (SD2SP2, or an SD adapter in slot A or B)");
                font_draw_center(320, 356, FS_BODY, C_TEXT_DIM, "then press A to try again");
            }
        } else if (screen == SCR_MENU) menu_draw();
        else screens_draw();
        ui_toasts_update_draw();
        video_frame_end();
    }
    return 0;
}
