@ Startup code for GBA cartridge programs: the ROM header, stacks, copying
@ IWRAM, data and EWRAM sections from ROM, clearing BSS, and calling main.

	.section .crt0, "ax", %progbits
	.arm
	.align	2
	.global	_start
_start:
	b	start_vector

	@ Nintendo logo, title, codes and header checksum are filled in by gbafix
	.fill	156, 1, 0
	.fill	12, 1, 0
	.fill	4, 1, 0
	.byte	0x30, 0x31
	.byte	0x96
	.byte	0x00
	.byte	0x00
	.fill	7, 1, 0
	.byte	0x00
	.byte	0x00
	.fill	2, 1, 0

start_vector:
	mov	r0, #0x12
	msr	cpsr_c, r0
	ldr	sp, =__sp_irq
	mov	r0, #0x1f
	msr	cpsr_c, r0
	ldr	sp, =__sp_usr

	ldr	r0, =__iwram_lma
	ldr	r1, =__iwram_start
	ldr	r2, =__iwram_end
	bl	copy_words
	ldr	r0, =__data_lma
	ldr	r1, =__data_start
	ldr	r2, =__data_end
	bl	copy_words
	ldr	r0, =__ewram_lma
	ldr	r1, =__ewram_start
	ldr	r2, =__ewram_end
	bl	copy_words

	ldr	r1, =__bss_start
	ldr	r2, =__bss_end
	bl	clear_words
	ldr	r1, =__sbss_start
	ldr	r2, =__sbss_end
	bl	clear_words

	ldr	r3, =__libc_init_array
	mov	lr, pc
	bx	r3

	mov	r0, #0
	mov	r1, #0
	ldr	r3, =main
	mov	lr, pc
	bx	r3
	b	.

copy_words:
	cmp	r1, r2
	ldrlo	r3, [r0], #4
	strlo	r3, [r1], #4
	blo	copy_words
	bx	lr

clear_words:
	mov	r0, #0
1:	cmp	r1, r2
	strlo	r0, [r1], #4
	blo	1b
	bx	lr

	.pool
