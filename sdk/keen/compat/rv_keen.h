/*
 * rv_keen.h — shared declarations for the Omnispeak riscv-stack port seam.
 *
 * The port is hal.h-direct (the Doom seam, not the Tyrian/Wolf3D SDL-shim
 * seam): Omnispeak's own backend tables (VL_Backend / IN_Backend /
 * SD_Backend) are exactly the right shape, so each compat/id_*_rvstack.c
 * implements one of them straight on top of soc/hal/hal.h. No sdl_lite /
 * sdl2_lite involved — noted in PORTING.md.
 *
 * Part of the Omnispeak riscv-stack port glue. SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef RV_KEEN_H
#define RV_KEEN_H

/* One-time platform bring-up (sys_init + pakfs mount + save regions).
 * Idempotent; called from FS_Startup — the first thing Omnispeak's main()
 * does — so every later subsystem can rely on the HAL being up. */
void RVK_PlatformInit(void);

/* Boot-progress beacon (the rvb_progress pattern): sys_diag 0xBEAC000n for
 * the sim testbench + a painted color bar for a hardware photo.
 *   1 = platform up (pak mounted, saves restored)
 *   2 = video mode set (VL_InitScreen ran)
 *   3 = sound manager up (SD backend started)
 *   4 = first present reached (game is drawing)
 */
void RVK_Beacon(int stage);

/* Service Omnispeak's 140/560 Hz t0 timer (SDL_t0Service) from wall time
 * (sys_ticks_us). NO threads on this platform: every wait loop and the
 * per-frame paths (present / pumpEvents / waitVBLs) call this. */
void RVK_TimerPump(void);

/* SELECT+START (or the in-game Quit path): flush persistent files
 * (save_commit) and return to the console's game picker (sys_exit). */
void RVK_QuitToPicker(void);

/* Persist every dirty user file to the HAL save window (called by the
 * quit paths; individual FS_CloseFile already commits its own file). */
void RVK_FS_FlushAll(void);

#endif /* RV_KEEN_H */
