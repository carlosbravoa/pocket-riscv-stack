/*
 * unistd.h — riscv-stack compat SHADOW header for the DOOM port.
 *
 * i_system.c includes <unistd.h> but uses nothing from it (on either the
 * console or the PC twin); picolibc-minimal may not ship one at all, so
 * this empty stand-in keeps the include portable. Deliberately no
 * include_next: nothing is needed from a real header.
 *
 * GPL-2.0-or-later (port glue; see ../ATTRIBUTION.md).
 */
#ifndef RVSTACK_UNISTD_SHADOW_H
#define RVSTACK_UNISTD_SHADOW_H
#endif
