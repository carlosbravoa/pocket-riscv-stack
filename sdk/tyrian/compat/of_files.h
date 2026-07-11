/*
 * of_files.h — OpenTyrian2000 riscv-stack port: file layer.
 *
 * Same interface as the openfpgaOS port's file seam (file.c / config.c /
 * opentyr.c dispatch on it unchanged), reimplemented for the riscv-stack
 * SDK: the whole Tyrian data set lives in one pakfs-format archive
 * (tyrian.pak, built with soc/tools/make_pakfs.py) pulled into free DRAM at
 * boot and served zero-copy; user read/write files (config, saves) become
 * session RAM files (see compat/rvfile.c).
 *
 * GPL-2.0-or-later (port glue; see compat/SDL.h).
 */
#pragma once

#include <stdio.h>

/* Sentinel "directory" strings returned by data_dir()/get_user_directory();
 * dir_fopen() dispatches on them. Never collide with a real path. */
#define OF_DATA_DIR "\x01" "PAKDATA"
#define OF_USER_DIR "\x01" "USERCFG"

/* Pull tyrian.pak from the Pocket Pak slot into DRAM and parse its
 * directory. Call once, early in main(). Halts with a UART message if no
 * pak is picked (the game can't run without its data). */
void of_files_init(void);

/* Open a data file from the in-memory pak (read-only). NULL if not present. */
FILE *of_pak_open(const char *name);

/* Open a user file (config/save) backed by a session RAM file. */
FILE *of_user_open(const char *name, const char *mode);

static inline int of_is_data_dir(const char *d) { return d && d[0] == '\x01' && d[1] == 'P'; }
static inline int of_is_user_dir(const char *d) { return d && d[0] == '\x01' && d[1] == 'U'; }
