# midiplay — Standard MIDI Files on the REAL OPL3

A .mid jukebox for the RISC-V Stack's FM flavor. The showcase point: the CPU
never synthesizes a sample. It parses the MIDI file, schedules events against
`sys_ticks_us()`, and forwards register writes to the OPL3 **in fabric** via
`opl_write()` — the same seam Tyrian's music uses (`compat/opl3_hw.c`), but
driven by General MIDI instead of a game's private format. No other Pocket
core plays SMF on actual FM silicon.

## Run it

Console (FM flavor):

    make            # -> midiplay.bin  (needs the SoC build tree; SD Game slot)
    make pak        # -> songs.pak     (SD, pick in the Pak slot)

PC twin (all development/verification happens here):

    make -f ../pc/pc.mk
    RVSTACK_FORCE_FM=1 RVSTACK_PAK=songs.pak ./midiplay-pc

Keys on the twin: arrows, Z=A, X=B, TAB=SELECT, ENTER=START, ESC quits.
On a flavor without FM hardware the app shows "FM CORE REQUIRED" and exits
cleanly via SELECT+START — one binary, any flavor.

### Controls

| where    | input        | action                                   |
|----------|--------------|------------------------------------------|
| browser  | UP/DOWN      | select song                               |
| browser  | A            | play                                      |
| playing  | A            | pause / resume (replay when done)         |
| playing  | LEFT/RIGHT   | previous / next song (jukebox wraps)      |
| playing  | B            | back to the browser                       |
| anywhere | SELECT       | cycle patch bank (re-voices live)         |
| anywhere | SELECT+START | exit to the console's picker              |

The bank choice persists in the game's save (`save_open("midiplay")`), so the
console remembers your favorite OPL sound across power cycles. Songs finish
into the next track automatically.

## Adding songs and banks

Drop files into `assets/` and rebuild the pak:

    assets/*.mid          songs (SMF format 0 or 1, PPQN timing)
    assets/banks/*        patch banks, cycled with SELECT
    make pak

Bank formats the loader understands (magic-sniffed, then extension):

* **.op2** — DMX GENMIDI (Doom): 128 melodic + 47 percussion. Complete GM.
  Double-voice entries use voice 1 only (halves channel pressure; the DMXOPL
  bank still reads well as single-voice).
* **.ibk** — Creative SBTimbre: 128 melodic. No percussion — drums fall back
  to the first .op2 in the pak.
* **.tmb** — Apogee Sound System (Duke3D, ROTT): 128 melodic + 128 drums.
* **.opl / .ad** — AIL/Miles Global Timbre Library (the classic `fat.opl`):
  bank 0 melodic, bank 127 drums.

Shipped banks (see `assets/LICENSES.txt` for full texts): `GENMIDI.op2` from
DMXOPL (MIT) and `allegro.ibk`, the Allegro library's GM set by Jorrit Rouwe
(giftware), converted by `tools/allegro_to_ibk.py`. Creative's
`standard.ibk`, game-shipped `fat.opl`/`.tmb` files etc. are copyrighted —
bring your own; the loader takes them as-is. Note that Windows' `gm.dls` is a
**wavetable** (DLS sample) bank, not FM patches — nothing to load on an OPL3;
use the formats above.

A bad bank file is rejected and the current bank stays; a bad .mid shows a
"skipped" toast. Neither can crash the player.

## Architecture

    main.c   UI (browser / now-playing / no-FM), the frame loop, the
             scheduler pump, bank catalog + save persistence
    smf.c    clean-room SMF reader: format 0/1, bounds-checked, zero-copy
             over the pak, per-track cursors merged in tick order
    bank.c   .op2/.ibk/.tmb/.opl loaders -> one normalized bank_ins_t
    oplgm.c  the GM performer: 18 two-op voices across both OPL3 register
             banks, oldest-note stealing, percussion on channel 10 via
             dedicated patches (melodic mode — no OPL rhythm mode),
             CC 7/10/11/64/120/121/123, pitch bend (+/-2 semitones),
             register shadow to keep the write stream minimal
    tables.h GENERATED (tools/gen_tables.py): F-number table in 1/32
             semitones and the GM volume->attenuation curve — every float
             lives in the generator, the player is integer-only (rv32im)

Timing: the pump runs once per frame around `fb_present()`. Tick→µs uses the
tempo map with a 64-bit remainder accumulator, so long songs never drift;
the song clock advances by `sys_ticks_us()` deltas (wrap-safe) and freezes
during pause. OPL3 NEW mode is set first (0x105=1) and every 0xC0 write
carries the L/R enables — the OPL2-program-on-OPL3 lesson inherited from
`sdk/tyrian/compat/opl3_hw.c`. Retrigger pacing is `opl_write()`'s job.

## Verification (PC twin, headless)

`RVSTACK_OPLLOG` captures every register write as `<t_us> <reg> <val>`;
`tools/check_opllog.py` then proves the stream against the source MIDI:
NEW-mode ordering, L/R bits on every 0xC0, per-channel key-on/key-off
alternation, and the tempo-mapped note-on schedule (`--tol-ms`, default 50).

    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy RVSTACK_FORCE_FM=1 \
    RVSTACK_PAK=songs.pak RVSTACK_OPLLOG=/tmp/a.log RVSTACK_INPUT=30:A \
    timeout 13 ./midiplay-pc
    tools/check_opllog.py /tmp/a.log assets/demo_groove.mid

Measured on this tree: worst note-on deviation **0.4–0.8 ms** across all
four shipped songs, including `demo_groove.mid`'s mid-song tempo change
(120→150 bpm), percussion and pitch bends. Two-bank A/B captures
(`--compare`) show identical timing with differing timbre streams, and a
save-restored bank reproduces its stream byte-for-byte.

## Console integration status

Everything above runs on the PC twin. The console binary builds via
`../game.mk` (untested here: needs the SoC build tree + RISC-V toolchain).
Expected differences on hardware: `fb_present()` paces at real vsync (the
pump granularity becomes ~16.7 ms — still far inside musical tolerance),
and `opl_write()` adds its retrigger guard between key-off/key-on pairs.

SPDX-License-Identifier: BSD-2-Clause (player code). Assets: see
`assets/LICENSES.txt`.
