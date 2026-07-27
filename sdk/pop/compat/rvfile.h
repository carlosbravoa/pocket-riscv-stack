/*
 * rvfile.h — FILE-stream shim internals for the SDLPoP riscv-stack port.
 * See compat/stdio.h (the shadow header) for how calls get here.
 *
 * Backing stores:
 *  - reads: zero-copy memory windows over the pakfs archive in DRAM
 *    (PRINCE.DAT, DIGISND1.DAT, ... — mounted lazily on first fopen), and
 *  - writes: named session-RAM files (sdlpop.cfg, quicksave.sav) that ride
 *    the HAL's per-game save for the slots listed in rvfile.c.
 *
 * Part of the SDLPoP riscv-stack port glue (see compat/SDL.h).
 */
#ifndef RVSTACK_POP_RVFILE_H
#define RVSTACK_POP_RVFILE_H

#include <stdio.h>
#include <stdint.h>

/* 0 = a pakfs archive is mounted (mounts on first call). */
int rvfs_pak_ready(void);

/* Direct pak lookup (NULL if absent) — for existence probes. */
const void *rvfs_pak_data(const char *name, uint32_t *size_out);

#endif /* RVSTACK_POP_RVFILE_H */
