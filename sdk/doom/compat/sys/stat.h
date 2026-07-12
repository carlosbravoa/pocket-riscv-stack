/*
 * sys/stat.h — riscv-stack compat SHADOW header for the DOOM port.
 *
 * m_misc.c's M_MakeDirectory() calls mkdir(); there are no directories on
 * either target (pakfs is flat, config/saves are named RAM files), so it
 * becomes a successful no-op. Nothing else from the real header is used.
 *
 * GPL-2.0-or-later (port glue; see ../ATTRIBUTION.md).
 */
#ifndef RVSTACK_SYS_STAT_SHADOW_H
#define RVSTACK_SYS_STAT_SHADOW_H

static inline int rvfs_mkdir_stub(void) { return 0; }
#define mkdir(...) rvfs_mkdir_stub()

#endif
