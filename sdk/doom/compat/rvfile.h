/*
 * rvfile.h — FILE-stream shim internals for the DOOM riscv-stack port.
 * See compat/stdio.h (the shadow header) for how calls get here.
 *
 * Two backing stores (the Tyrian port's pattern):
 *  - read-only memory windows over the DRAM-resident pak (zero copy) — the
 *    IWAD and anything else inside doom.pak, and
 *  - named RAM files for the game's writes (default.cfg, doomsav*.dsg).
 *    Only default.cfg is persisted through the HAL save API; savegames
 *    live for the session (see PORTING.md — 32 KB save budget).
 *
 * GPL-2.0-or-later (port glue; see ../ATTRIBUTION.md).
 */
#ifndef RVSTACK_RVFILE_H
#define RVSTACK_RVFILE_H

#include <stdio.h>
#include <stdint.h>

/* Call once after pakfs_mount_at(), before doomgeneric_Create(). */
void rvfs_files_init(void);

#endif /* RVSTACK_RVFILE_H */
