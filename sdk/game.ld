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

    /* DWARF unwind data for C++ exceptions. C-only ports (the common case)
     * emit little or none of this and never register it, so it costs a few
     * bytes and is otherwise inert. C++ ports (OpenJazz) MUST keep it: the
     * DWARF unwinder needs .eh_frame to walk the stack, and a game that
     * DISCARDs it turns every `throw` into terminate()->abort()->reset (a
     * silent reboot loop — cost a full night to isolate on OpenJazz). The
     * trailing zero word is the terminator __register_frame() scans for;
     * crt0 has no crtend.o to add it. Registered by rvstack_eh_init() (called
     * from a C++ port's main before the first throw).
     *
     * MUST come BEFORE .bss: .bss is NOLOAD, so a LOADable section placed
     * after it makes `objcopy -O binary` fill the entire .bss gap with zeros
     * (SkyRoads' 1.7 MB roads[] array ballooned the .bin 20 KB -> 1.76 MB
     * before this was moved). Keeping it in the loaded run of sections keeps
     * the flat binary tight. */
    .eh_frame :
    {
        . = ALIGN(4);
        __eh_frame_start = .;
        KEEP(*(.eh_frame))
        LONG(0);
        __eh_frame_end = .;
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

    /DISCARD/ : { *(.comment) }
}
