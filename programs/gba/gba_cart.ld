/* Linker script for GBA cartridge programs, used through gba.specs */
OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)

MEMORY {
	rom   : ORIGIN = 0x08000000, LENGTH = 32M
	iwram : ORIGIN = 0x03000000, LENGTH = 32K
	ewram : ORIGIN = 0x02000000, LENGTH = 256K
}

__sp_irq = 0x03007FA0;
__sp_usr = 0x03007F00;

SECTIONS {
	.crt0 : { KEEP(*(.crt0)) } > rom
	.init : { KEEP(*(SORT_NONE(.init))) } > rom
	.text : {
		*(.text .text.* .stub .gnu.linkonce.t.*)
		*(.glue_7t) *(.glue_7) *(.vfp11_veneer) *(.v4_bx)
	} > rom
	.fini : { KEEP(*(SORT_NONE(.fini))) } > rom
	.rodata : { *(.rodata .rodata.* .gnu.linkonce.r.*) } > rom
	.ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > rom
	.ARM.exidx : {
		__exidx_start = .;
		*(.ARM.exidx* .gnu.linkonce.armexidx.*)
		__exidx_end = .;
	} > rom
	.preinit_array : {
		PROVIDE_HIDDEN(__preinit_array_start = .);
		KEEP(*(.preinit_array))
		PROVIDE_HIDDEN(__preinit_array_end = .);
	} > rom
	.init_array : {
		PROVIDE_HIDDEN(__init_array_start = .);
		KEEP(*(SORT(.init_array.*)))
		KEEP(*(.init_array))
		PROVIDE_HIDDEN(__init_array_end = .);
	} > rom
	.fini_array : {
		PROVIDE_HIDDEN(__fini_array_start = .);
		KEEP(*(SORT(.fini_array.*)))
		KEEP(*(.fini_array))
		PROVIDE_HIDDEN(__fini_array_end = .);
	} > rom
	. = ALIGN(4);

	.iwram : ALIGN(4) {
		__iwram_start = .;
		*(.iwram .iwram.* .iwram*)
		. = ALIGN(4);
		__iwram_end = .;
	} > iwram AT > rom
	__iwram_lma = LOADADDR(.iwram);

	.data : ALIGN(4) {
		__data_start = .;
		*(.data .data.* .gnu.linkonce.d.*)
		. = ALIGN(4);
		__data_end = .;
	} > iwram AT > rom
	__data_lma = LOADADDR(.data);

	.bss (NOLOAD) : ALIGN(4) {
		__bss_start = .;
		*(.bss .bss.* .gnu.linkonce.b.*)
		*(COMMON)
		. = ALIGN(4);
		__bss_end = .;
	} > iwram

	.ewram : ALIGN(4) {
		__ewram_start = .;
		*(.ewram .ewram.*)
		. = ALIGN(4);
		__ewram_end = .;
	} > ewram AT > rom
	__ewram_lma = LOADADDR(.ewram);

	.sbss (NOLOAD) : ALIGN(4) {
		__sbss_start = .;
		*(.sbss .sbss.*)
		. = ALIGN(4);
		__sbss_end = .;
	} > ewram

	PROVIDE(end = .);
	PROVIDE(__end__ = .);
	PROVIDE(__heap_limit = ORIGIN(ewram) + LENGTH(ewram));
}
