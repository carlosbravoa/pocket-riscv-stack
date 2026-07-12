/*
 * rvfile.h — FILE-stream shim internals for the Wolf4SDL riscv-stack port.
 * See compat/stdio.h (the shadow header) for how calls get here.
 *
 * Backing stores:
 *  - reads: zero-copy memory windows over the pakfs archive in DRAM
 *    (vswap.wl1, vgagraph.wl1, ... — mounted lazily on first fopen), and
 *  - writes: named session-RAM files (config.wl1, savegam?.wl1) that ride
 *    the HAL's per-game save for the slots listed in rvfile.c.
 *
 * Part of the Wolf4SDL riscv-stack port glue (see compat/SDL.h).
 */
#ifndef RVSTACK_WOLF_RVFILE_H
#define RVSTACK_WOLF_RVFILE_H

#include <stdio.h>
#include <stdint.h>

/* 0 = a pakfs archive is mounted (mounts on first call). */
int rvfs_pak_ready(void);

/* Direct pak lookup (NULL if absent) — for existence probes. */
const void *rvfs_pak_data(const char *name, uint32_t *size_out);

#endif /* RVSTACK_WOLF_RVFILE_H */
