// Skyroads-c on riscv-stack: pak-backed asset loading.
//
// Upstream loads assets with per-file *_path() wrappers around read_file();
// every loader also has a *_bytes(Bytes) entry point, so the console needs no
// filesystem shim at all — this module mounts skyroads.pak and hands each
// asset's bytes to those loaders. Names in the pak are lowercased
// (make_pakfs.py packs data/, pakfs is case-sensitive).
#pragma once

#include "data/byteio.hpp"

namespace rvstack {

// Mount the pak: a manually picked Pak slot wins; with none picked, auto-bind
// "skyroads.pak" from /Assets/riscv_stack/common/ (pak_bind_named semantics).
// Returns 0, or negative on failure (caller decides how loudly to die).
int assets_mount();

// Whole-file bytes for an asset ("roads.lzs"). Fatal-diags and hangs if the
// file is missing: every asset this port asks for is required to run.
skyroads::data::Bytes asset_bytes(const char* name);

} // namespace rvstack
