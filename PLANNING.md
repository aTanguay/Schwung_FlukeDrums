# FlukeDrums — Planning & Architecture

How FlukeDrums works, why it's built this way, and the constraints any change has
to respect. For the task list see [TASKS.md](TASKS.md). For day-to-day
conventions see [CLAUDE.md](CLAUDE.md).

## Vision

A creativity tool, not a sample manager. The fun is in **happy accidents**:
grab random sounds from a big pool, keep what sparks something, throw the rest
back. Every feature should make that loop faster or more surprising:

```
roll → play → lock keepers → roll → … → save
```

Non-goals (for now): sample editing, tagging or databases, a full kit editor.
Move and MrDrums already handle shaping sounds.

## Where it sits

- **Ableton Move**: aarch64 Linux groovebox, closed firmware.
- **[Schwung](https://github.com/charlesvestal/schwung)**: unofficial framework
  that runs alongside Move's firmware and loads modules (`module.json` +
  native `dsp.so` + optional JS UI). FlukeDrums is a **sound generator** module in
  a Schwung shadow slot, played by a Move track via MIDI channel routing.
- **[MrDrums](https://github.com/handcraftedcc/schwung-mrdrums)** (MIT): a
  16-pad sample player. FlukeDrums is a fork: MrDrums' engine, pad parameters and
  `.ablpreset` import, with the reroll layer added on top.

### Why fork MrDrums

It already had what's needed underneath (16 pads on notes 36–51, per-pad
parameters persisted as `p01_*`…`p16_*`, a WAV/AIFF decoder, Move kit import).
FlukeDrums adds one self-contained engine (`src/dsp/reroll.{h,cpp}`) plus glue in
`mrdrums_plugin.cpp`. The diff stays reviewable, and the async-loading fixes can
go back upstream (see TASKS.md, "Upstream").

## The hard constraint: the audio thread

Schwung calls **every** plugin entry point (`create_instance`, `set_param`,
`get_param`, `on_midi`, `render_block`) on the SPI audio callback: SCHED_FIFO
90, core 3, roughly 900 µs per 128-frame block. Blocking there drops out audio
for the **whole device**, including Move's own tracks. So on those paths:

- no file I/O (`fopen`, `opendir`, `stat`, …)
- no `malloc` / `free` / `new` / `delete`
- no locks a non-realtime thread can hold
- no logging (even `fprintf(stderr)` is a syscall)
- no unbounded work

`get_param` is called on every UI repaint, so it must only *format* data that
already exists.

Upstream MrDrums broke this in two places, both fixed here:
1. it decoded WAV files inside `set_param`;
2. `render_block` allocated two `std::vector`s per block (now stack scratch,
   chunked at 256 frames).

## Threading model

```
 audio thread (SPI callback)                 worker (SCHED_OTHER, cores 0–2)
 ───────────────────────────                 ───────────────────────────────
 set_param("rr_roll") ── cmds ring (64) ──▶  scan pool · pick · decode · copy
                                                       │      folder scan
 render_block:                                         │
   rr_poll()  ◀──────── ready ring (48) ───────────────┘  decoded buffer + path
   swap pad pointer
   old buffer → retired[] (kept while a voice plays it)
   rr_retire() ──────── retired ring (256) ─▶  free()

 get_param("chain_params"):
   rr_folders() ◀── atomic pointer to an immutable folder list (never freed until destroy)
```

- **Rings** are lock-free single-producer/single-consumer with preallocated
  slots. The audio side never allocates or locks.
- **Worker demotes itself first.** A thread spawned from the callback inherits
  FIFO 90 and would starve Move. Its first act is `SCHED_OTHER` plus affinity
  to cores 0–2, keeping it off the audio core.
- **Safe swap.** Voices copy `sample_data` at note-on, so a swapped-out buffer
  may still be playing. The audio thread holds it in `retired[]` until no
  active voice references it, then hands it to the worker to free. No clicks,
  no use-after-free.
- **Coalescing.** Pool changes and folder scans queued back-to-back (spinning
  the Folder knob) collapse to the last one. File-browser live preview fires a
  load per cursor step, and only the newest per pad is decoded.
- **Folder list lifetime.** Each scan builds a new list and publishes it with a
  release store. Old lists are kept until `rr_destroy`, so a pointer the audio
  thread loaded can never be freed or rewritten under it. Scans are rare, and
  capped at 64 per instance.
- **Fallback.** If the worker can't start, `inst->rr` is NULL and MrDrums'
  original synchronous loading is used.

## Worker behaviour

- **Pool scan.** Recursive (12 levels max, 200k files max), `.wav/.aif/.aiff`.
  It skips dot-files (including macOS `._` junk), never follows directory
  symlinks, and always skips the Save Kit folder so saved kits don't feed back
  into the pool.
- **Roll(mask).** For each pad in the mask, it tries up to 24 random
  candidates, preferring files not already on that pad or elsewhere in this
  roll. A cheap size check skips obvious stems before decoding, and the decoded
  length is then checked exactly against Max Len. The undo snapshot is recorded
  **only if something landed**, so a no-op roll can't push the oldest entry off
  a full undo stack.
- **Undo.** 32-deep stack of 16-path snapshots. Only pads that differ reload.
- **Save Kit.** Creates `…/Samples/Schwung/FlukeDrums/Fluke YYYY-MM-DD HHMMSS/`,
  copies each pad as `NN <basename>`, and writes `flukedrums-kit.json` with sources.
- **Folder scan.** Walks each library root (`My` → `UserLibrary/Samples`,
  `Core` → `CoreLibrary/Samples`), counts audio per folder recursively, and
  keeps folders with files down to depth 3. Over 128 entries (Schwung's enum
  option limit), it keeps shallow folders first, then sorts into tree order.
  Labels (`My/Drums/Kicks (412)`) are unique and act as the wire value.

## Parameters

| Key | Kind | Saved in set |
|---|---|---|
| `rr_pool_dir` | filepath; a picked file is reduced to its folder | yes |
| `rr_recursive` | enum off/on | yes |
| `rr_max_len` | float 0–10 s | yes |
| `p01_lock`…`p16_lock`, alias `pad_lock` | enum off/on | yes |
| `rr_folder` | dynamic enum, `options_as_string`; label ↔ path | via `rr_pool_dir` |
| `rr_roll`, `rr_roll_pad`, `rr_undo`, `rr_commit`, `rr_rescan`, `rr_unlock_all`, `rr_pool_up` | trigger: `access:"write"`, options `["-","Roll!"]` | no |
| `rr_status`, `rr_last_kit`, `rr_pool_count`, `rr_undo_depth`, `rr_busy` | readout: `access:"read"` | no |

Triggers fire on their option name or index `1`. A value of `"-"` or `"0"`
does nothing, so a stray index-0 write can't wipe a kit. (Schwung's docs
describe exactly this hazard in another module.)

## Build & binary constraints

- Built with **Zig** for `aarch64-linux-gnu.2.28`: the binary needs at most
  glibc 2.27. The Move's glibc version isn't documented; MrDrums releases built
  against 2.35 run on it, so 2.28 is comfortably safe. CI enforces ≤ 2.31.
- The C++ runtime (libc++) is linked statically.
- **Only `move_plugin_init_v2` is exported** (`scripts/exports.map`). Exporting
  the bundled runtime (`operator new`, …) could interpose on Move or other
  modules in the same process. CI checks there's exactly one export.
- Ubuntu 24.04's cross-gcc is *not* used: its libstdc++ and C23 headers pull in
  glibc 2.36/2.38 symbols (`arc4random`, `__isoc23_strtol`), which the Move
  likely lacks.
- `module.json` must stay under Schwung's 8 KB loader cap. Per-pad defaults
  are applied by the DSP, not listed there.

## Testing strategy

| Layer | How |
|---|---|
| Engine (`reroll.cpp`) | `tests/reroll_engine_test.cpp`, with a fake loader and simulated audio thread, under ASan/UBSan and TSan |
| Full plugin | `tests/reroll_workflow_test.cpp` drives the real plugin API with generated WAVs: roll, lock, undo, roll pad, 40 rolls while playing, Save Kit, folder picker, Up One, knob-spin coalescing, state round-trip, destroy mid-roll |
| ARM64 | the same tests cross-compiled with Zig and run under `qemu-aarch64`, plus a `dlopen` smoke test of the shipped `dsp.so` |
| Device | manual, on a Move |

Sanitizer runs gate everything in CI (`.github/workflows/test.yml`).
`settle()` in the tests waits on wall-clock time (up to 120 s) and fails loudly
on timeout, because TSan slows decoding 5–20× and a silent cap once made a
correct-but-slow roll look like a bug.

## Decisions log

| Date | Decision | Why |
|---|---|---|
| 2026-10-31 | Fork MrDrums instead of starting fresh | Engine, pad params and kit import already existed; keeps diff small and upstreamable |
| 2026-10-31 | All loading on one worker thread | Schwung realtime rules; a reroll is 16 decodes |
| 2026-10-31 | Undo snapshot only when a roll lands | Found by the engine test: no-op rolls were eating undo history |
| 2026-10-31 | Build with Zig pinned to glibc 2.28 | Ubuntu 24.04 cross-gcc output needed glibc 2.38 |
| 2026-10-31 | Export only the entry point | Statically linked runtime must not leak into the shared process |
| 2026-10-31 | Folder picker as a dynamic enum, not Schwung's file browser | The browser can't select folders, and its knob-turn view shows one folder's files with no way up |
| 2026-10-31 | Library roots: My + Core | Covers user samples and Ableton's library; Up One stops at the root so the pool can't swallow system folders |
