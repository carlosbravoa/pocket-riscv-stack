/*
 * cxx_rt.cpp — the freestanding C++ runtime OpenJazz needs on rv32.
 *
 * OpenJazz is "C with classes": 54 .cpp files, classes + virtuals + new/delete,
 * and ZERO STL (`grep -c 'std::'` == 0). So the only runtime pieces required
 * are operator new/delete, the pure-virtual handler, and the static-init guard
 * — everything else the compiler emits inline. We build with -fno-exceptions
 * -fno-rtti, which drops the unwinder and typeinfo entirely and keeps the
 * binary close to a C build.
 *
 * Deliberately NOT linking libstdc++: it would drag in locales, iostreams and
 * an exception personality routine that picolibc-minimal cannot satisfy.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stdlib.h>
#include <stddef.h>

/* gamelib.c provides a real K&R malloc/free over sbrk. */

void *operator new(size_t n)            { return malloc(n ? n : 1); }
void *operator new[](size_t n)          { return malloc(n ? n : 1); }
void  operator delete(void *p)          { free(p); }
void  operator delete[](void *p)        { free(p); }
void  operator delete(void *p, size_t)  { free(p); }
void  operator delete[](void *p, size_t){ free(p); }

/* Placement new is header-only; nothing to emit. */

extern "C" {

/* A call through a pure virtual is a bug; park loudly rather than run off. */
void __cxa_pure_virtual(void) { for (;;) ; }
void __cxa_deleted_virtual(void) { for (;;) ; }

/* Guards for function-local statics. Single-threaded, so these are trivial —
 * the default implementations live in libstdc++, which we do not link. */
int  __cxa_guard_acquire(long long *g) { return !*(char *)g; }
void __cxa_guard_release(long long *g) { *(char *)g = 1; }
void __cxa_guard_abort(long long *g)   { (void)g; }

/* Static destructors never run: the console exits by resetting the SoC. */
int __cxa_atexit(void (*f)(void *), void *p, void *d)
{ (void)f; (void)p; (void)d; return 0; }

void *__dso_handle = 0;

/* ---- DWARF exception-frame registration --------------------------------
 * crt0_game jumps straight to main with no crtbegin/crtend, so libgcc's
 * automatic __register_frame_info (normally driven by crtbegin's ctor) never
 * runs. Register .eh_frame explicitly so the unwinder can find the FDEs and
 * `throw` actually reaches its `catch` instead of calling terminate(). The
 * linker script keeps .eh_frame with these bounds + a zero terminator.
 * Call rvstack_eh_init() ONCE at the very top of main(), before any throw. */
extern char __eh_frame_start[];
void __register_frame(void *begin);

void rvstack_eh_init(void)
{
	static int done;
	if (done) return;
	done = 1;
	__register_frame((void *)__eh_frame_start);
}

} /* extern "C" */
