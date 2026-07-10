// gamelib.c — runtime support linked into every GAME (not the bootloader).
//
// Heap: everything between the end of the game image (_end, from game.ld) and
// the top of the game region is malloc()'s. picolibc's allocator calls sbrk().
//
// SPDX-License-Identifier: BSD-2-Clause

#include <stddef.h>
#include <stdint.h>
#include <errno.h>

extern char _end;                              // from game.ld: after .bss

#define HEAP_LIMIT ((char *)0x42000000)        // end of the 28 MB game region

void *sbrk(ptrdiff_t incr)
{
	static char *heap;
	if (!heap)
		heap = &_end;
	if (heap + incr > HEAP_LIMIT || heap + incr < &_end) {
		errno = ENOMEM;
		return (void *)-1;
	}
	char *prev = heap;
	heap += incr;
	return prev;
}
