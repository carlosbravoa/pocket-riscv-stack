// pakfs — many named files inside one .pak (the port library's filesystem).
//
// Real games open dozens of data files by name; the Pocket gives us ONE
// user-picked Pak slot. tools/make_pakfs.py packs a directory into a .pak;
// this reads it in place (zero copy — the whole pak sits in DRAM).
//
//   pakfs_mount();                        // once, after sys_init()
//   pakfs_file_t f;
//   if (pakfs_open("data/music.mus", &f) == 0) {
//       pakfs_read(buf, 1, 16, &f);      // fread-shaped on purpose (ports)
//       pakfs_seek(&f, 0, PAKFS_SEEK_SET);
//   }
//
// Layout (little-endian, produced by make_pakfs.py):
//   0x00  "PAKF"        0x04 u32 version=1     0x08 u32 nfiles   0x0C u32 rsvd
//   0x10  nfiles x { char name[48]; u32 offset; u32 size; }   (56 B/entry)
//   data  (4-byte aligned, offsets from pak start)
//
// SPDX-License-Identifier: BSD-2-Clause
#ifndef RVSTACK_PAKFS_H
#define RVSTACK_PAKFS_H

#include <stdint.h>

#define PAKFS_NAME_MAX 47

typedef struct {
	const uint8_t *base;                // file's bytes, directly in DRAM
	uint32_t       size;
	uint32_t       pos;
} pakfs_file_t;

#define PAKFS_SEEK_SET 0
#define PAKFS_SEEK_CUR 1
#define PAKFS_SEEK_END 2

// Pull the Pak slot and validate the directory. 0 = ok; -1 = no pak picked /
// load failed; -2 = not a pakfs (plain single-file paks still work via
// pak_open()). Safe to call again after the user re-picks a pak.
int pakfs_mount(void);
int pakfs_mount_at(uint32_t dst_off);   // big paks: land above the game region

int pakfs_nfiles(void);                              // -1 if not mounted
const char *pakfs_name(int i);                       // iterate the directory

// 0 = found. Zero-copy: prefer pakfs_data() when you just want the bytes.
int pakfs_open(const char *name, pakfs_file_t *f);
const void *pakfs_data(const char *name, uint32_t *size_out);

// fread/fseek/ftell shapes, so ports drop in with thin wrappers.
uint32_t pakfs_read(void *dst, uint32_t sz, uint32_t n, pakfs_file_t *f);
int      pakfs_seek(pakfs_file_t *f, int32_t off, int whence);
uint32_t pakfs_tell(const pakfs_file_t *f);

#endif // RVSTACK_PAKFS_H
