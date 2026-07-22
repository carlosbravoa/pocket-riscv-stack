/* setjmp.h — shadow. picolibc-minimal doesn't link setjmp/longjmp; provide a
 * rv32 implementation (compat/libc_shim.c). PoP uses one setjmp/longjmp pair
 * (start_game restart). PC twin uses the real header. */
#ifndef POP_COMPAT_SETJMP_H
#define POP_COMPAT_SETJMP_H
#ifdef RVSTACK_PC
#include_next <setjmp.h>
#else
typedef long jmp_buf[16];          /* ra, sp, s0-s11 = 14 words (spare) */
int  setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val) __attribute__((noreturn));
#endif
#endif
