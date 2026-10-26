/* emusd.h - the card in memory the EMU_SD build mounts. Not in the shipping
 * build; see emusd.c. */
#pragma once
#ifdef EMU_SD
#include <ogc/disc_io.h>
extern DISC_INTERFACE __io_emusd;
#endif
