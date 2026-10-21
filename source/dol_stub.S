/* dol_stub.S - position-independent copier that runs from low memory once the
 * operating system is gone. r3 points at a table:
 *   u32 nsec; then nsec x { u32 dst, u32 src, u32 len (multiple of 4) };
 *   u32 bss_addr, u32 bss_len (multiple of 4); u32 entry.
 * It copies every section, zeroes the bss, writes back and invalidates the
 * caches over all of main memory, then jumps to the entry point. */
	.text
	.globl dol_stub_begin, dol_stub_end
dol_stub_begin:
	lwz	4, 0(3)
	addi	5, 3, 4
1:	cmpwi	4, 0
	beq	3f
	lwz	6, 0(5)
	lwz	7, 4(5)
	lwz	8, 8(5)
	srwi	8, 8, 2
	cmpwi	8, 0
	beq	9f
	mtctr	8
	subi	6, 6, 4
	subi	7, 7, 4
2:	lwzu	9, 4(7)
	stwu	9, 4(6)
	bdnz	2b
9:	addi	5, 5, 12
	subi	4, 4, 1
	b	1b
3:	lwz	6, 0(5)
	lwz	7, 4(5)
	lwz	10, 8(5)
	srwi	7, 7, 2
	cmpwi	7, 0
	beq	5f
	mtctr	7
	li	9, 0
	subi	6, 6, 4
4:	stwu	9, 4(6)
	bdnz	4b
5:	lis	6, 0x8000
	lis	7, 0x000C		/* 24 MB / 32 bytes = 0xC0000 lines */
	mtctr	7
6:	dcbst	0, 6
	addi	6, 6, 32
	bdnz	6b
	sync
	lis	6, 0x8000
	lis	7, 0x000C
	mtctr	7
7:	icbi	0, 6
	addi	6, 6, 32
	bdnz	7b
	sync
	isync
	mtlr	10
	blr
dol_stub_end:
