/* emusd_img.S - the card image the EMU_SD build mounts (see emusd.c). In
 * .data because emusd.c writes to it. Built from the project root, so the
 * path is relative to the build folder. */
#ifdef EMU_SD
	.section .data
	.balign 32
	.global emusd_img
emusd_img:
	.incbin "../data/emusd/sdcard.img"
	.global emusd_img_end
emusd_img_end:
#endif
