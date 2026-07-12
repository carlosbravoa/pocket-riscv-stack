/* Linker script for RISC-V stack GAMES (flat binaries run from DRAM).
 *
 * The ROM bootloader pulls game.bin to 0x40400000 (GAME_RAM_OFFSET in main_ram)
 * and jumps to its first byte — so .text.start MUST come first. Stack lives in
 * the 16 KB on-chip SRAM (fast, and entirely ours once the bootloader is left).
 * Everything after _end (up to the top of the 64 MB main_ram) is the game's
 * free RAM/heap; the region below 0x40400000 holds the framebuffers (+0) and
 * the asset pak area (+1 MB) — don't touch those directly, use the HAL.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
OUTPUT_FORMAT("elf32-littleriscv")
ENTRY(_start)

MEMORY {
    game : ORIGIN = 0x40400000, LENGTH = 0x01C00000  /* 28 MB for code+data */
    /* Stack lives at the TOP of the game DRAM region, growing down, with
     * 1 MB of headroom above the heap ceiling (0x41F00000, gamelib.c).
     * It used to be the 16 KB on-chip SRAM — fatal for ported code:
     * Tyrian's JE_loadMap() alone carries a ~76 KB frame (v0.19.x hangs). */
    stackram : ORIGIN = 0x41F00000, LENGTH = 0x100000
}

SECTIONS
{
    .text :
    {
        *(.text.start)          /* entry — must be the first byte of the binary */
        *(.text .text.*)
    } > game

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        . = ALIGN(4);
    } > game

    .data :
    {
        . = ALIGN(4);
        __global_pointer$ = . + 0x800;
        *(.data .data.*)
        *(.sdata .sdata.*)
        . = ALIGN(4);
    } > game

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _fbss = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > game

    _end = .;                                   /* heap starts here */
    PROVIDE(_fstack = ORIGIN(stackram) + LENGTH(stackram) - 8);

    /DISCARD/ : { *(.eh_frame) *(.comment) }
}
