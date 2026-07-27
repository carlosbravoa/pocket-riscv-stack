/* unaligned.h — safe unaligned/endian buffer access for ports.
 *
 * WHY THIS EXISTS (PORTABILITY.md trap #3): VexiiRiscv TRAPS FATALLY on a
 * misaligned word access — red bars, parked CPU. The PC twin (x86) tolerates
 * the same access silently, so casting a byte buffer to a u16/u32 pointer
 * (file-format parsing, packed structs, network byte order) is the #1
 * hardware-only crash in ports. Use these helpers instead of pointer casts:
 *
 *     uint32_t len   = una_rd_u32le(hdr + 7);      // any alignment, any target
 *     int16_t  delta = (int16_t)una_rd_u16le(p);   // signed: just cast
 *
 * They compile to the OPTIMAL sequence per target — a single load on x86
 * (the PC twin), individual byte loads + shifts on rv32 (trap-impossible,
 * ~4-7 cycles from L1, cheaper than one trip through any trap handler could
 * ever be). GCC/Clang recognize the byte-or-shift idiom and fold it into a
 * single load where legal; there is no function-call cost at -O1 and up.
 *
 * The LE forms match every DOS-era file format (and RIFF/WAV/BMP/PCX);
 * the BE forms cover IFF/network-order assets. Aligned access does NOT need
 * these — a uint32_t* you know is 4-aligned is fine to deref directly.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef RVSTACK_UNALIGNED_H
#define RVSTACK_UNALIGNED_H

#include <stdint.h>

/* ---- reads, little-endian (DOS/RIFF/BMP/PCX/most game data) ---- */

static inline uint16_t una_rd_u16le(const void *p)
{
	const uint8_t *b = (const uint8_t *)p;
	return (uint16_t)(b[0] | ((uint16_t)b[1] << 8));
}

static inline uint32_t una_rd_u32le(const void *p)
{
	const uint8_t *b = (const uint8_t *)p;
	return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
	       ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

/* ---- reads, big-endian (IFF/network-order assets) ---- */

static inline uint16_t una_rd_u16be(const void *p)
{
	const uint8_t *b = (const uint8_t *)p;
	return (uint16_t)(((uint16_t)b[0] << 8) | b[1]);
}

static inline uint32_t una_rd_u32be(const void *p)
{
	const uint8_t *b = (const uint8_t *)p;
	return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
	       ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

/* ---- writes, little-endian ---- */

static inline void una_wr_u16le(void *p, uint16_t v)
{
	uint8_t *b = (uint8_t *)p;
	b[0] = (uint8_t)v;
	b[1] = (uint8_t)(v >> 8);
}

static inline void una_wr_u32le(void *p, uint32_t v)
{
	uint8_t *b = (uint8_t *)p;
	b[0] = (uint8_t)v;
	b[1] = (uint8_t)(v >> 8);
	b[2] = (uint8_t)(v >> 16);
	b[3] = (uint8_t)(v >> 24);
}

/* ---- writes, big-endian ---- */

static inline void una_wr_u16be(void *p, uint16_t v)
{
	uint8_t *b = (uint8_t *)p;
	b[0] = (uint8_t)(v >> 8);
	b[1] = (uint8_t)v;
}

static inline void una_wr_u32be(void *p, uint32_t v)
{
	uint8_t *b = (uint8_t *)p;
	b[0] = (uint8_t)(v >> 24);
	b[1] = (uint8_t)(v >> 16);
	b[2] = (uint8_t)(v >> 8);
	b[3] = (uint8_t)v;
}

#endif /* RVSTACK_UNALIGNED_H */
