# Attribution & licenses

- `src/` — Wolf4SDL v2.0 by Moritz "Ripper" Kroll and the community
  (github.com/11001011101011/Wolf4SDL fork lineage, vendored from
  github.com/11001011101001011/Wolf4SDL @ dc8b250), based on
  Wolfenstein 3D by id Software. License: `src/license-id.txt`
  (id Software's original terms) with optional GPL parts documented in
  `src/license-gpl.txt`. The DOSBox/MAME OPL emulators were NOT vendored
  (the FM flavor has a real OPL3; `compat/id_sd_rv.c` replaces id_sd.c).
- `compat/` — riscv-stack port glue; it accompanies the game sources and
  inherits their licensing. The SDK it talks to (`sdk/`, `soc/hal`)
  stays BSD-2-Clause.
- `data/` — Wolfenstein 3D shareware v1.4 episode data files. Freely
  distributable per `data/vendor.doc` (id Software's shareware vendor
  terms); do not add registered `.wl6` data to the repository.
