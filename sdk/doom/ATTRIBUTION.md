# DOOM (doomgeneric) for the riscv-stack SDK — attribution

- `src/` is vendored from **doomgeneric** (https://github.com/ozkl/doomgeneric),
  upstream commit `dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284` (2026-04-12),
  itself derived from id Software's DOOM via Chocolate Doom / fbDOOM.
  License: **GPL-2.0-or-later** — see `LICENSE` in this directory.
  The per-platform backends (`doomgeneric_sdl.c`, `_xlib.c`, `_win.c`, ...,
  `i_sdlsound.c`, `i_sdlmusic.c`, `i_allegro*.c`) are not vendored; this
  port's backend is `compat/dg_rvstack.c` against the SDK's `hal.h`.
- `compat/` is riscv-stack port glue, GPL-2.0-or-later (it inherits the
  game's license; the SDK it talks to stays BSD-2-Clause).
- The test asset is the **shareware** `doom1.wad` (freely distributable,
  id Software). It is not committed to the repo — `make wad` fetches it.
