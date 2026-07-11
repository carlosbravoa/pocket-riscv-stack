/*
 * rvfile.h — FILE-stream shim internals for the OpenTyrian2000 riscv-stack
 * port. See compat/stdio.h (the shadow header) for how calls get here.
 *
 * Two backing stores:
 *  - read-only memory windows over the DRAM-resident pak (zero copy), and
 *  - named RAM files for the game's config/save writes ("staged saves":
 *    they live for the session; wiring them to the HAL save_open/save_commit
 *    API is a noted follow-up).
 *
 * GPL-2.0-or-later (port glue; see compat/SDL.h).
 */
#ifndef RVSTACK_RVFILE_H
#define RVSTACK_RVFILE_H

#include <stdio.h>
#include <stdint.h>

/* Open a read-only stream over a memory window (pak file contents). */
FILE *rvfs_open_mem(const void *data, uint32_t size);

/* Open (or create) a named session-RAM file. mode: "r*"/"w*"/"a*". */
FILE *rvfs_open_user(const char *name, const char *mode);

#endif /* RVSTACK_RVFILE_H */
