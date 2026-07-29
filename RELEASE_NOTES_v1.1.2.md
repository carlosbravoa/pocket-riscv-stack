# v1.1.2 — SkyRoads, and the bugs it flushed out

A new game, a fixed loader, five resurrected binaries, and a round of
voice-mixer hardening. Both flavors are in the one family zip as always;
the base and FM cores are ABI 1.2 (append-only — every existing `.bin`
keeps working).

## New: SkyRoads

A faithful port of the 1993 Bluemoon classic, from the
[Skyroads-c](https://github.com/carlosbravoa/SDL-skyroads) C++17
reimplementation — the real TREKDAT rasteriser, the EXE's own 5-substep
ship physics, and MUZAX music on the console's real OPL3.

Getting it to run well on a 74 MHz in-order core with no FPU took four
things, each of which is now reusable by any port:

- **An indexed renderer.** The reimplementation drew RGBA; the DOS game
  drew 8-bit indices through a palette it reprogrammed per frame — which
  is exactly what this console's framebuffer and CLUT are. Re-plumbed
  upstream and proven pixel-exact against 1230 baseline frame hashes.
- **The DOS write budget.** A pure "rebuild every pixel" renderer costs
  ~450 KB of writes per frame. The incremental path caches the backdrop
  per road and rewrites only the road band, the previous ship rect and
  the dashboard — byte-identical output, verified by a 2000-frame
  dual-render comparison.
- **Decode-once geometry.** The shape cursor re-parsed the compressed
  TREKDAT stream for every shape of every frame. That, not the pixel
  loops, was the 10 fps.
- **Integer physics.** The ship simulation emulated DOS fixed-point in
  `double`; on a soft-float CPU that alone consumed the entire 36 Hz
  frame budget. It now runs in the EXE's own integer representation,
  with the full DEMO.REC attract playback as the equivalence proof.

Pad: d-pad steers and throttles, A jumps, START selects, B backs out,
SELECT+START quits to the picker (progress is saved first). Progress
lives in the original's own 66-byte `skyroads.cfg` block.

## Fixed: the Pak loader

`target_dataslot_openfile` resolves paths from the **SD root only** —
never from the platform's asset directory. Auto-binding a pak by name
silently failed for every game that tried it. `pak_bind_named()` now
prefixes `/Assets/riscv_stack/common/`, and the simulator's testbench
models the same root-relative semantics so this cannot regress.

## Fixed: five games that had been broken since v0.21

doom, wolf3d, keen, quabricks and midiplay were last built at v0.20.0.
One day later, a commit inserted three CSRs into the middle of the `main`
register block and shifted the audio and pak registers up 12 bytes. Those
binaries kept the old addresses: they poked the audio register believing
it was the pak request, and waited forever on a register that no longer
meant "busy". Doom bounced back to the loader; Wolf3D hung on the loading
screen. All five are rebuilt against the current map, and their port
branches are re-synced to the v1.1 SDK (so they also pick up the 64-bit
timebase and hardware voices).

The golden ABI lock postdates that shift, so this class is closed —
appends only, guarded on every build.

## Voice mixer

- Two-slot parity line cache per voice: the interpolator reads samples
  *n* and *n+1*, which straddle a cache line every few samples and used
  to evict each other twice per frame.
- A pending-tick latch so a frame that overruns its budget catches up
  instead of silently dropping a 48 kHz tick, and a fill watchdog so a
  wedged DRAM read can never silence the mixer permanently.
- New debug registers (ABI append) reporting the two samples and the
  fraction the interpolator last used, plus its result — the mixer's
  arithmetic is now observable from software instead of only audible.

**Known issue:** the mixer still renders *fractional* playback rates
incorrectly on silicon, while a rate of exactly 48 kHz is perfect. Games
that feed 48 kHz samples are unaffected (SkyRoads converts at load).
Diagnosis continues; `sdk/sfxtest` and `sdk/vmxprobe` reproduce it.

## Also

- **FM: "Exit to Menu" works again.** The fix landed on the base branch
  and was never mirrored to FM, so every FM core had shipped it grayed
  out.
- SkyRoads' FM music is attenuated 9 dB against the sample effects: the
  console sums a real OPL3 at full scale against mixer voices the
  hardware halves for centre pan, a balance the desktop mix never had.
- A far-right fall off the road no longer freezes the game (a rectangle
  entirely off-screen produced a negative-width copy).
- Build process: the FM flavor must be mirrored before building, the
  family zip freezes its assets at core-build time, and a build whose
  compiler flags changed now force-cleans — all three shipped a wrong
  artifact at least once and are now documented and guarded.
