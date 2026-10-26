/* emusd.c - a card that lives in memory, so the program can be tested in an
 * emulator.
 *
 * Dolphin has no SD card device for the GameCube (its EXI device list has
 * memory cards, the BBA and a USB Gecko, and nothing else), so without this
 * everything behind "No SD card found" is out of reach there. With EMU_SD=1
 * the build bakes the FAT image tools/mksdimg.sh makes into the DOL and
 * mounts it from RAM: reads and writes both work, and the writes are gone
 * when the program exits.
 *
 * Only compiled when EMU_SD=1 is passed to make; the shipping build is the
 * same as without this file.
 */
#ifdef EMU_SD
#include "emusd.h"
#include <gccore.h>
#include <string.h>

#define SECTOR_SIZE 512

/* emusd_img.S puts the card image in .data, so writes stay in it */
extern u8 emusd_img[], emusd_img_end[];

static u32 sector_count(void) { return (u32)(emusd_img_end - emusd_img) / SECTOR_SIZE; }

static bool in_range(sec_t sector, sec_t count) { return sector + count <= sector_count(); }

static bool emusd_startup(DISC_INTERFACE *disc) { (void)disc; return true; }
static bool emusd_is_inserted(DISC_INTERFACE *disc) { (void)disc; return true; }
static bool emusd_clear(DISC_INTERFACE *disc) { (void)disc; return true; }
static bool emusd_shutdown(DISC_INTERFACE *disc) { (void)disc; return true; }

static bool emusd_read(DISC_INTERFACE *disc, sec_t sector, sec_t count, void *buffer) {
    (void)disc;
    if (!in_range(sector, count)) return false;
    memcpy(buffer, emusd_img + sector * SECTOR_SIZE, (size_t)count * SECTOR_SIZE);
    return true;
}

static bool emusd_write(DISC_INTERFACE *disc, sec_t sector, sec_t count, const void *buffer) {
    (void)disc;
    if (!in_range(sector, count)) return false;
    memcpy(emusd_img + sector * SECTOR_SIZE, buffer, (size_t)count * SECTOR_SIZE);
    return true;
}

static bool emusd_erase(DISC_INTERFACE *disc, sec_t sector, sec_t count) {
    (void)disc;
    if (!in_range(sector, count)) return false;
    memset(emusd_img + sector * SECTOR_SIZE, 0, (size_t)count * SECTOR_SIZE);
    return true;
}

DISC_INTERFACE __io_emusd = {
    ('E' << 24) | ('M' << 16) | ('S' << 8) | 'D',
    FEATURE_MEDIUM_CANREAD | FEATURE_MEDIUM_CANWRITE | FEATURE_MEDIUM_CANERASE,
    emusd_startup,
    emusd_is_inserted,
    emusd_read,
    emusd_write,
    emusd_erase,
    emusd_clear,
    emusd_shutdown,
    0,              /* libfat takes the geometry from the image's own boot sector */
    1,
    SECTOR_SIZE,
};
#endif
