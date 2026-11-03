# Changelog

## Unreleased

### Added
- **Kit Mode** (Pool page, knob 1; on by default). The bottom two rows of pads
  now roll a real kit, found by sample names: pads 1–2 kick, 3 snare, 4 clap,
  5 closed hat, 6 open hat, 7 crash/ride, 8 tom/perc. The top two rows stay
  completely random. Names are matched by word, so `808BD_01`, `KickHard`,
  `Kicks/thump.wav` and Ableton's `Drums/Kick/...` all count as kicks, and
  `ABDUCT.wav` doesn't. Loops, breaks, fills and vocals are never taken for
  hits. A part with no match borrows a neighbour (no claps → a snare, no open
  hats → a closed hat, no toms → perc) and otherwise goes random.
- **Roll Pad keeps the pad's part**: rerolling the snare pad gives another snare.
- **Kit Parts** readout (Pool page): how many of the 8 kit parts your current
  pool can fill by name, e.g. `6/8`.
- Saved kits' `flukedrums-kit.json` now says what each pad is (`"kind": "kick"`).

### Changed
- Rolls avoid any sample already in the kit, including locked pads (they
  used to avoid only this roll's picks).
- Pool page: Kit Mode is knob 1 and Kit Parts replaces Undos on the knobs;
  Undos and Last Kit move to the page after it.

## 0.4.1 — 2026-11-02

### Changed
- **Undo respects locks and your edits.** Undo now puts back only the pads
  the last roll changed, and skips any pad you've locked since or replaced by
  hand. Lock the one good hit from a bad roll, then Undo: the keeper stays.

### Fixed
- **Distortion on loud kits.** The output soft clip (inherited from MrDrums)
  dropped anything just over 0.95 down to 0.74, folding every hot peak into a
  notch. It now saturates smoothly toward full scale.
- **Loading a Move kit preset no longer reads the file on the audio thread.**
  The `.ablpreset` is parsed in the background and applied on the next audio
  block; browsing presets quickly only loads the one you land on. A preset
  with an unsupported sample link now changes nothing (it used to apply the
  pads listed before that link, then stop).

## 0.4.0 — 2026-11-01

### Changed
- **Menus in workflow order.** Page 1's knobs now read left to right as the
  steps of a session:

  | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
  |---|---|---|---|---|---|---|---|
  | Folder | Up One | Max Len | **Roll** | Lock | Roll Pad | Undo | Save Kit |

  Then **Pad**, **Pad 2**, **Pool**, **Global**. Previously Roll was on the
  second page behind the pad tone knobs.
- Each control now appears once (the pad tone controls were on two pages).
- Undo is kept three knobs from Roll, since a knob turn fires it on the grid;
  Unlock All moved off page 1 to the Pool page.
- Help screens: FlukeDrums pages first, the inherited sampler pages grouped
  under **Sound Engine**; log lines say `[flukedrums]`.

### Added
- `scripts/preview-pages.mjs`: shows the knob-grid pages using Schwung's own
  page planner, for checking layout changes.

### Fixed
- CI: the test runner lost its executable bit in the rename.

## 0.3.0 — 2026-11-01

### Changed
- **Renamed from Reroll to FlukeDrums.** Module id is now `flukedrums`, so it
  installs as a new module; remove the old "Reroll" module in Schwung Manager.
- Saved kits now go to `Samples/Schwung/FlukeDrums/Fluke <date>/` with a
  `flukedrums-kit.json` manifest (kits saved by Reroll stay where they were).
- Release tarball is now `flukedrums-module.tar.gz`.

## 0.2.0 — 2026-10-31

### Added
- **Folder picker** (first page and knob 8): pick the pool from a list of your
  sample folders with sample counts. `My` = your Samples folder, `Core` =
  Ableton's library; three levels deep, up to 128 folders.
- **Up One**: widen the pool one folder level (stops at the library root).
- Docker-free build: `scripts/build-zig.sh`.

### Changed
- The old file-browser pool picker is now **Browse** on the Pool page.
- Spinning through folders only scans the folder you land on.

## 0.1.0 — 2026-10-31

First release, forked from MrDrums 0.0.4.

### Added
- Roll all unlocked pads, Roll Pad, per-pad Lock, Undo Roll (32 levels),
  Save Kit (files + `reroll-kit.json` manifest), Rescan, Unlock All.
- Pool settings: folder, Subfolders, Max Len.
- Background worker for all disk work; samples swap without clicks or dropouts.

### Fixed (vs. MrDrums)
- Sample files were decoded on the audio thread.
- `render_block` allocated memory every block.
