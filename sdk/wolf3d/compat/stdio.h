/*
 * stdio.h — riscv-stack compat SHADOW header for the Wolf4SDL port.
 *
 * This directory is first on the include path, so every game TU sees this
 * file instead of the system stdio.h; it chains to the real header via
 * include_next and re-routes the FILE-stream API onto compat/rvfile.c
 * (pakfs-backed reads + RAM-staged writes riding the HAL save API).
 * Wolf opens ALL its data by plain fopen("vswap.wl1"), so unlike Tyrian
 * no game code changes are needed — the reroute IS the file layer.
 *
 * On the console, LiteX's picolibc-minimal has no fopen/fread at all; on
 * the PC twin the reroute makes the game read from the pak instead of the
 * working directory — same seam, both targets (trap #1: never let a game
 * TU bind to the real libc streams for data files).
 *
 * printf/puts/putchar/vfprintf are NOT touched (console: debug UART).
 * stderr/stdout become sentinels that rvfs_fprintf forwards to printf.
 *
 * Part of the Wolf4SDL riscv-stack port glue (see compat/SDL.h).
 */
#ifndef RVSTACK_WOLF_STDIO_SHADOW_H
#define RVSTACK_WOLF_STDIO_SHADOW_H

#include_next <stdio.h>

/* libc may implement these as macros over its own FILE */
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
int    rvfs_fflush(FILE *f);
int    rvfs_putchar(int c);
int    rvfs_remove(const char *path);

#define fopen   rvfs_fopen
#define fclose  rvfs_fclose
#define fread   rvfs_fread
#define fwrite  rvfs_fwrite
#define fseek   rvfs_fseek
#define ftell   rvfs_ftell
#define rewind  rvfs_rewind
#define fgetc   rvfs_fgetc
#define getc    rvfs_fgetc
#define fputc   rvfs_fputc
#define putc    rvfs_fputc
#define feof    rvfs_feof
#define ferror  rvfs_ferror
#define fputs   rvfs_fputs
#define fprintf rvfs_fprintf
#define fflush  rvfs_fflush
#define putchar rvfs_putchar
#define remove  rvfs_remove
#define unlink  rvfs_remove

#endif /* RVSTACK_WOLF_STDIO_SHADOW_H */
