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
