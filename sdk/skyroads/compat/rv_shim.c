/*
 * rv_shim.c — the few libc pieces -lstdc++ wants beyond libc_shim.c.
 *
 * OpenJazz links only libsupc++; SkyRoads links full libstdc++ (std::string /
 * std::vector out-of-line pieces), whose __verbose_terminate_handler prints
 * the exception type with fwrite before dying. There is nowhere for that
 * output to go on the console — swallow it and let abort() (libc_shim.c)
 * raise the 0xDEADxxxx diag path via trap.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stddef.h>

size_t fwrite(const void *ptr, size_t size, size_t nmemb, void *stream)
{
	(void)ptr; (void)size; (void)stream;
	return nmemb;
}

/* Newlib's libm (fmod & co, pulled by -lm for the ship sim's doubles) reports
 * domain errors through its reentrant errno accessor. Single-threaded: one
 * static cell. Picolibc's errno is a different mechanism; nothing reads this. */
int *__errno(void)
{
	static int e;
	return &e;
}
