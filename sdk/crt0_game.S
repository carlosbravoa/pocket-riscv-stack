/* Game startup: the bootloader jumps to the first byte of the binary (this).
 * .data is already in place (flat binary, loaded as-is); we set up gp/sp, zero
 * .bss and call main(). DRAM + video scanout are already live (sys_init() in
 * the game adopts them — see hal.c warm-start path).
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
    .section .text.start
    .global _start
_start:
    /* Interrupts OFF, permanently: games are pure polling (the HAL never
     * registers handlers), and the trap vector we inherit points into the
     * bootloader ROM whose ISR state lives in SRAM — which is about to become
     * our stack. Any stray PLIC line (UART RX noise on the floating debug
     * pin, a late TX-empty event) would vector into that corrupted ISR and
     * kill the game. Found by soc/sim (## PLIC: Unhandled claim). */
    csrci mstatus, 0x8          /* mstatus.MIE = 0 masks everything; note this
                                 * VexiiRiscv config has NO mie CSR (PLIC MMIO
                                 * instead) — csrw mie traps illegal! */
    la    t0, rvstack_trap      /* real exceptions: paint red bars + park
                                 * (gamelib.c) — visible on hardware */
    csrw  mtvec, t0

    .option push
    .option norelax
    la   gp, __global_pointer$
    .option pop
    la   sp, _fstack

    /* zero .bss */
    la   a0, _fbss
    la   a1, _ebss
1:  bgeu a0, a1, 2f
    sw   zero, 0(a0)
    addi a0, a0, 4
    j    1b
2:
    call main
    /* main returned: spin (the bootloader treats a return as game exit only
       if the game returns from its entry — which lands here instead; a real
       exit protocol can come later). */
3:  j    3b

