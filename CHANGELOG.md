# Changelog

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
