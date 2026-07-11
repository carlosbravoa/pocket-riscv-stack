# OpenTyrian2000 — riscv-stack port: attribution & licensing

## Origin

- **Game engine**: `src/` contains the **OpenTyrian2000** sources
  (Copyright (C) 2007-2009 The OpenTyrian Development Team,
  Copyright (C) 2022 Kaito Sinclaire and contributors) — the open-source
  engine for the DOS shmup *Tyrian 2000*. All original license headers are
  preserved. `src/opl.c`/`src/opl.h` are the DOSBox Team's OPL2/OPL3
  emulation library (LGPL-2.1-or-later, based on Ken Silverman's ADLIBEMU).
- **Port seams cribbed from**: the *opentyrian-fpga* port of the same game
  to the openfpgaOS Pocket stack (`/home/carlos/devel/fpga/opentyrian-fpga`),
  whose `of_files.*` file-layer interface, `video.c` indexed-present path,
  `opentyr.c` init hooks (`of_files_init()`, `music_disabled = true`) and
  Makefile defines this port reuses. Those seams were GPL per that project's
  documentation.
- **Port glue written here**: everything under `compat/` plus the small
  `RVSTACK` patches in `src/video.c` and `src/opentyr.c`, targeting the
  riscv-stack SDK's `sdl_lite`/`pakfs`/HAL interfaces.

## Data files

`data/` (packed into `tyrian.pak`) are the **Tyrian 2000 data files**,
released as **freeware** by the original publisher. They are not GPL; they
are redistributable as-is.

## Licensing

- **This game (sources in `src/` and `compat/`, and the built `tyrian.bin`)
  is GPL-2.0-or-later.** The compat glue inherits the game's license.
- **The riscv-stack SDK stays BSD-2-Clause.** The game links against the
  SDK (`sdl_lite`, `pakfs`, `gamelib`, HAL) the same way any game does; GPL
  applies to this game binary only and is isolated to `sdk/tyrian/`.
