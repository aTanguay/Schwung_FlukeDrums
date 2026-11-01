# Changelog

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
