/*
 * stdio.h — riscv-stack compat SHADOW header for the DOOM port.
 *
 * This directory is first on the include path, so every game TU sees this
 * file instead of the system stdio.h; it chains to the real header via
 * include_next and re-routes the FILE-stream API Doom uses onto
 * compat/rvfile.c (pakfs-backed reads + RAM-staged writes). On the console
 * LiteX's picolibc-minimal has no fopen/fread/... at all and its FILE is
 * not ours, so everything stream-shaped must be intercepted at compile
 * time; on the PC twin the same interception routes the WAD/config/save
 * traffic through the identical pakfs/HAL path — the twin tests the seam,
 * not the host filesystem. (Pattern proven by sdk/tyrian/compat/stdio.h.)
 *
 * printf/puts/putchar are NOT touched: they are the libc's and go to the
 * debug UART (console) / terminal (PC). stdout/stderr become sentinels
 * that rvfs_fprintf/rvfs_fputs/rvfs_vfprintf recognize and forward.
 *
 * Doom additions over the Tyrian shadow: vfprintf (I_Error), fscanf
 * (m_config), remove/rename (g_game's savegame commit dance).
 *
 * GPL-2.0-or-later (port glue; see ../ATTRIBUTION.md).
 */
#ifndef RVSTACK_STDIO_SHADOW_H
#define RVSTACK_STDIO_SHADOW_H

#include_next <stdio.h>
#include <stdarg.h>

/* the libc may implement these as macros over its own FILE */
#undef getc
#undef putc
#undef getchar
#undef putchar
#undef feof
#undef ferror
#undef stdin
#undef stdout
#undef stderr

#define RVFS_STDIN  ((FILE *)1)
#define RVFS_STDOUT ((FILE *)2)
#define RVFS_STDERR ((FILE *)3)
#define stdin  RVFS_STDIN
#define stdout RVFS_STDOUT
#define stderr RVFS_STDERR

FILE  *rvfs_fopen(const char *path, const char *mode);
int    rvfs_fclose(FILE *f);
size_t rvfs_fread(void *dst, size_t size, size_t n, FILE *f);
size_t rvfs_fwrite(const void *src, size_t size, size_t n, FILE *f);
int    rvfs_fseek(FILE *f, long off, int whence);
long   rvfs_ftell(FILE *f);
void   rvfs_rewind(FILE *f);
int    rvfs_fgetc(FILE *f);
int    rvfs_fputc(int c, FILE *f);
int    rvfs_feof(FILE *f);
int    rvfs_ferror(FILE *f);
int    rvfs_fputs(const char *s, FILE *f);
int    rvfs_fprintf(FILE *f, const char *fmt, ...)
           __attribute__((format(printf, 2, 3)));
int    rvfs_vfprintf(FILE *f, const char *fmt, va_list ap);
int    rvfs_fscanf(FILE *f, const char *fmt, ...);
int    rvfs_fflush(FILE *f);
int    rvfs_putchar(int c);
int    rvfs_remove(const char *path);
int    rvfs_rename(const char *from, const char *to);

#define fopen    rvfs_fopen
#define fclose   rvfs_fclose
#define fread    rvfs_fread
#define fwrite   rvfs_fwrite
#define fseek    rvfs_fseek
#define ftell    rvfs_ftell
#define rewind   rvfs_rewind
#define fgetc    rvfs_fgetc
#define getc     rvfs_fgetc
#define fputc    rvfs_fputc
#define putc     rvfs_fputc
#define feof     rvfs_feof
#define ferror   rvfs_ferror
#define fputs    rvfs_fputs
#define fprintf  rvfs_fprintf
#define vfprintf rvfs_vfprintf
#define fscanf   rvfs_fscanf
#define fflush   rvfs_fflush
#define putchar  rvfs_putchar
#define remove   rvfs_remove
#define rename   rvfs_rename

/* picolibc-minimal ships no s*printf/sscanf either; compat/libc_shim.c
 * provides console definitions matching the include_next prototypes. */

#endif /* RVSTACK_STDIO_SHADOW_H */
