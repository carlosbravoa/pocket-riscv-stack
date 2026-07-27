/* dirent.h — shadow: picolibc-minimal has no <dirent.h>. SDLPoP scans real
 * dirs only for the loose-file "find by extension" rescue in locate_file;
 * on console the pak resolves files by exact path, so opendir returns NULL
 * and the scan is inert (stubs live in compat/libc_shim.c). On the PC twin the
 * real dirent is used (compat/ is not on the twin's SDK include path... but IS
 * on the game group's — the twin has real dirent though, so guard to the real
 * one there via include_next). */
#ifndef POP_COMPAT_DIRENT_H
#define POP_COMPAT_DIRENT_H
#ifdef RVSTACK_PC
#include_next <dirent.h>
#else
typedef struct RVDIR DIR;
struct dirent { char d_name[256]; };
DIR *opendir(const char *name);
struct dirent *readdir(DIR *d);
int  closedir(DIR *d);
#endif
#endif
