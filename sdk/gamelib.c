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

// ---------------------------------------------------------------------------
// Trap beacon: crt0_game points mtvec here. A CPU exception mid-game paints
// RED BARS top+bottom on both pages and parks — so on hardware a "black
// screen hang" splits into "CPU trapped" (red bars) vs "system/bus hang"
// (no bars). mcause lands on the diag port for the sim TB.
// ---------------------------------------------------------------------------
#include "hal.h"

void rvstack_trap(void)
{
	unsigned long cause;
	__asm__ volatile("csrr %0, mcause" : "=r"(cause));
	sys_diag(0xDEAD0000u | (unsigned)(cause & 0xFFFF));
	for (int page = 0; page < 2; page++) {
		uint8_t *fb = fb_backbuffer();
		for (int y = 0; y < 6; y++) {
			for (int x = 0; x < 320; x++) {
				fb[y * 320 + x] = 0xE0;                 // rgb332 red
				fb[(239 - y) * 320 + x] = 0xE0;
			}
		}
		fb_present();
	}
	for (;;)
		;
}

#define HEAP_LIMIT ((char *)0x41F00000)        // stack owns the top 1 MB (game.ld)

void *sbrk(ptrdiff_t incr);

// ---------------------------------------------------------------------------
// malloc/free/calloc/realloc — classic K&R free-list allocator over sbrk.
// LiteX links picolibc-MINIMAL, which ships no allocator at all; ports
// (SDL surfaces, Tyrian) need a real one. First-fit, coalescing on free.
// ---------------------------------------------------------------------------

// picolibc-minimal also lacks memcmp (ports and pakfs users need it)
int memcmp(const void *a, const void *b, size_t n)
{
	const unsigned char *x = a, *y = b;
	for (size_t i = 0; i < n; i++)
		if (x[i] != y[i])
			return x[i] - y[i];
	return 0;
}

// LiteX's libc fallback memcpy/memset are 1-byte-per-iteration loops — every
// byte costs a separate DRAM store (~13 cycles/byte, fbbench stage 1). Full-
// surface copies dominate 2D rendering, so override them with word-wide
// versions. Word loops run only when src/dst are co-aligned (this CPU traps
// on misaligned word access); surfaces and pitches are word-multiples, so
// that's the common case. gamelib.o builds with -fno-builtin so these loops
// can't be pattern-matched back into memcpy/memset calls.
void *memcpy(void *dst, const void *src, size_t n)
{
	unsigned char *d = dst;
	const unsigned char *s = src;
	if (n >= 16 && ((((uintptr_t)d ^ (uintptr_t)s) & 3) == 0)) {
		while ((uintptr_t)d & 3) { *d++ = *s++; n--; }
		uint32_t *dw = (uint32_t *)d;
		const uint32_t *sw = (const uint32_t *)s;
		while (n >= 16) {
			dw[0] = sw[0]; dw[1] = sw[1];
			dw[2] = sw[2]; dw[3] = sw[3];
			dw += 4; sw += 4; n -= 16;
		}
		while (n >= 4) { *dw++ = *sw++; n -= 4; }
		d = (unsigned char *)dw;
		s = (const unsigned char *)sw;
	}
	while (n--) *d++ = *s++;
	return dst;
}

void *memset(void *dst, int c, size_t n)
{
	unsigned char *d = dst;
	if (n >= 16) {
		uint32_t w = (unsigned char)c * 0x01010101u;
		while ((uintptr_t)d & 3) { *d++ = (unsigned char)c; n--; }
		uint32_t *dw = (uint32_t *)d;
		while (n >= 16) {
			dw[0] = w; dw[1] = w; dw[2] = w; dw[3] = w;
			dw += 4; n -= 16;
		}
		while (n >= 4) { *dw++ = w; n -= 4; }
		d = (unsigned char *)dw;
	}
	while (n--) *d++ = (unsigned char)c;
	return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
	if ((uintptr_t)dst <= (uintptr_t)src ||
	    (uintptr_t)dst >= (uintptr_t)src + n)
		return memcpy(dst, src, n);       // no overlap hazard forward
	unsigned char *d = (unsigned char *)dst + n;
	const unsigned char *s = (const unsigned char *)src + n;
	if (n >= 16 && ((((uintptr_t)d ^ (uintptr_t)s) & 3) == 0)) {
		while ((uintptr_t)d & 3) { *--d = *--s; n--; }
		uint32_t *dw = (uint32_t *)d;
		const uint32_t *sw = (const uint32_t *)s;
		while (n >= 4) { *--dw = *--sw; n -= 4; }
		d = (unsigned char *)dw;
		s = (const unsigned char *)sw;
	}
	while (n--) *--d = *--s;
	return dst;
}

typedef union header {
	struct { union header *next; size_t size; } s;   // size in header units
	long double align;
} header_t;

static header_t  base_hdr;
static header_t *freep;

void free(void *ap)
{
	if (!ap)
		return;
	header_t *bp = (header_t *)ap - 1, *p;
	for (p = freep; !(bp > p && bp < p->s.next); p = p->s.next)
		if (p >= p->s.next && (bp > p || bp < p->s.next))
			break;                          // at one end of the arena
	if (bp + bp->s.size == p->s.next) {     // join upper neighbor
		bp->s.size += p->s.next->s.size;
		bp->s.next  = p->s.next->s.next;
	} else
		bp->s.next = p->s.next;
	if (p + p->s.size == bp) {              // join lower neighbor
		p->s.size += bp->s.size;
		p->s.next  = bp->s.next;
	} else
		p->s.next = bp;
	freep = p;
}

void *malloc(size_t nbytes)
{
	size_t nunits = (nbytes + sizeof(header_t) - 1) / sizeof(header_t) + 1;
	header_t *p, *prevp = freep;
	if (!prevp) {
		base_hdr.s.next = freep = prevp = &base_hdr;
		base_hdr.s.size = 0;
	}
	for (p = prevp->s.next; ; prevp = p, p = p->s.next) {
		if (p->s.size >= nunits) {
			if (p->s.size == nunits)
				prevp->s.next = p->s.next;
			else {
				p->s.size -= nunits;
				p += p->s.size;
				p->s.size = nunits;
			}
			freep = prevp;
			return (void *)(p + 1);
		}
		if (p == freep) {                   // wrapped: grow the arena
			size_t grow = nunits < 1024 ? 1024 : nunits;   // >=16 KB steps
			header_t *up = (header_t *)sbrk((ptrdiff_t)(grow * sizeof(header_t)));
			if (up == (header_t *)-1)
				return 0;
			up->s.size = grow;
			free((void *)(up + 1));
			p = freep;
		}
	}
}

void *calloc(size_t n, size_t sz)
{
	size_t total = n * sz;
	void *p = malloc(total);
	if (p) {
		char *c = p;
		for (size_t i = 0; i < total; i++)
			c[i] = 0;
	}
	return p;
}

void *realloc(void *old, size_t nbytes)
{
	if (!old)
		return malloc(nbytes);
	header_t *h = (header_t *)old - 1;
	size_t have = (h->s.size - 1) * sizeof(header_t);
	if (have >= nbytes)
		return old;
	void *p = malloc(nbytes);
	if (p) {
		const char *s = old;
		char *d = p;
		for (size_t i = 0; i < have; i++)
			d[i] = s[i];
		free(old);
	}
	return p;
}

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

// picolibc-minimal also lacks rand/srand (ports: piece bags, shuffles).
// Classic 32-bit LCG (Numerical Recipes constants), RAND_MAX-compatible.
static uint32_t rand_state = 1;
void srand(unsigned s) { rand_state = s ? s : 1; }
int rand(void)
{
	rand_state = rand_state * 1664525u + 1013904223u;
	return (int)(rand_state >> 1);      // 31-bit non-negative
}
