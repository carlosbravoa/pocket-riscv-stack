# Getting Tyrian running — supplying the game data

The console ships the Tyrian **engine** (`tyrian.bin`, our OpenTyrian2000 port —
GPL, source in `sdk/tyrian/`). It does **not** ship the game **data**; you supply
that yourself from the freely-distributed Tyrian data files and pack it into a
`tyrian.pak` the engine reads. This is the standard "bring-your-own-data" model
every open-source game port uses.

The data files were released as **freeware** by the original publisher — they are
redistributable, but they are a separate thing from the GPL engine.

---

## Which data set?

Our port is **OpenTyrian2000**, so it looks for up to **five** episodes.

| Data set | Link | Episodes | Notes |
|---|---|---|---|
| **Tyrian 2000** (recommended) | `tyrian2000.zip` (e.g. archive.org / the OpenTyrian project) | 1–5 | Full match for this port. |
| **Tyrian 2.1** | http://camanis.net/tyrian/tyrian21.zip | **1–4** | Works fine — episode 5 (a Tyrian 2000 exclusive) simply won't appear. |

The engine scans for each `levelsN.dat` and only offers the episodes it finds
(`JE_scanForEpisodes`), so **Tyrian 2.1 boots and plays episodes 1–4 cleanly**;
it just won't list "Episode 5". Use Tyrian 2000 data if you want all five.

---

## Steps (using `tyrian21.zip`)

You need Python 3 and the packer at `soc/tools/make_pakfs.py`.

```sh
# 1. Download and unzip the freeware data
curl -L -o tyrian21.zip http://camanis.net/tyrian/tyrian21.zip
unzip tyrian21.zip                 # -> a tyrian21/ folder full of .dat/.shp/.snd/...

# 2. Pack that folder into tyrian.pak
#    Point the packer at the FOLDER whose *contents* are the data files, so each
#    file is stored at the pak root (e.g. "levels1.dat", not "tyrian21/levels1.dat").
python3 soc/tools/make_pakfs.py tyrian21/ tyrian.pak

# 3. Put the engine + data on the Pocket SD card
cp tyrian.pak /path/to/POCKET_SD/Assets/riscv_stack/common/
#    tyrian.bin is already there from the release zip; if not, copy it too.
```

## Running it on the Pocket

1. Load the **RISC-V Stack** (or **RISC-V Stack FM** for music) core.
2. In the Pocket menu, **Game slot → `tyrian.bin`**.
3. In the Pocket menu, **Pak slot → `tyrian.pak`**.
   The engine DMAs the pak into DRAM at boot and reads all its data from there.
4. The Tyrian title screen appears; pick an episode and play.

On the **FM** core you get hardware OPL FM music; on the base core the game runs
silent-music but fully playable (it detects FM via the HAL).

---

## Gotchas

- **Pack the contents, not the wrapper folder.** `make_pakfs.py tyrian21/ tyrian.pak`
  stores `levels1.dat` at the root. If you accidentally pack a parent folder you'll
  get `tyrian21/levels1.dat` and the engine won't find anything.
- **Lowercase filenames.** The engine opens `levels1.dat`, `tyrian.hdt`, `music.mus`
  … in lowercase. `tyrian21.zip` is already lowercase; if you source data from a
  DOS disk with UPPERCASE names, lowercase them before packing (pakfs is
  case-sensitive).
- **Filenames ≤ 47 chars** (a pakfs limit) — never an issue for Tyrian's short DOS
  8.3 names.
- **Missing episode 5 is normal** with 2.1 data — not an error.
- The Pocket never delivers a file's final 2 bytes (an APF quirk); `make_pakfs.py`
  already pads for this, so don't worry about it.

## Verifying the pak

```sh
# The pak is a plain archive; its stored names are visible as strings:
strings tyrian.pak | grep -E '\.(dat|shp|snd|mus|hdt)$' | head
# You should see levels1.dat, music.mus, tyrian.hdt, tyrian.shp, ...
```

---

## Licensing recap

- **Engine** (`sdk/tyrian/src` + `compat`, and `tyrian.bin`): **GPL-2.0-or-later**.
  Source is in this repo; that satisfies the GPL for the distributed binary.
- **Data** (`tyrian.pak`): the publisher's **freeware** Tyrian data — redistributable
  as-is, not GPL. See `sdk/tyrian/ATTRIBUTION.md`.
- **SDK** it links against (`pakfs`, `sdl_lite`, `gamelib`, HAL): **BSD-2-Clause**.
