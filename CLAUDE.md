# CLAUDE.md

Guidance for AI coding agents (and humans) working on Reroll. Read this before
changing code. The *why* behind it is in [PLANNING.md](PLANNING.md); open work
is in [TASKS.md](TASKS.md).

## What this is

A Schwung sound-generator module for Ableton Move: a fork of MrDrums that fills
16 pads with random samples, with lock / reroll / undo / save. The C++ DSP
(`src/dsp/`) is the whole module. There's no custom JS UI yet; the on-device UI
comes from `chain_params` + `ui_hierarchy`, served by the DSP.

## Layout

```
src/
  module.json            static metadata + fallback hierarchy (keep < 8 KB!)
  help.json              on-device help (lines ≤ 20 chars)
  dsp/
    reroll.h / .cpp      the reroll engine: worker thread, rings, scan, roll, undo, save, folder list
    mrdrums_plugin.cpp   plugin API glue (MrDrums + Reroll params, async swap, UI contracts)
    mrdrums_engine.*     MrDrums voice engine (mostly untouched)
    mrdrums_params.*     param tables (globals + per-pad fields)
scripts/
  build-zig.sh           ★ the build (no Docker). Same as CI.
  build.sh               Docker alternative inherited from MrDrums
  install.sh             scp to move.local
  exports.map            only move_plugin_init_v2 is exported
tests/
  run_reroll_tests.sh    ★ runs everything under ASan/UBSan + TSan
  reroll_engine_test.cpp engine only, fake loader
  reroll_workflow_test.cpp full plugin via the real API, generated WAVs
  upstream/              MrDrums' original tests (not run; see README there)
docs/                    USER_GUIDE.md, RELEASING.md, upstream-mrdrums/
```

## Commands

```bash
./tests/run_reroll_tests.sh     # all tests, both sanitizers. Run before every commit.
pip install ziglang             # once
./scripts/build-zig.sh          # → dist/reroll-module.tar.gz
./scripts/install.sh            # copy to the Move over SSH (then restart Schwung)
```

ARM64 check without a Move (Linux, `apt install qemu-user g++-aarch64-linux-gnu`
for the sysroot): cross-compile the workflow test with
`python3 -m ziglang c++ -target aarch64-linux-gnu.2.28 …` and run it with
`qemu-aarch64 -L /usr/aarch64-linux-gnu`.

## Rules that are easy to break

1. **Nothing blocking on the audio thread.** `set_param`, `get_param`,
   `on_midi`, `render_block`, `create_instance` and `destroy_instance` all run on
   Schwung's realtime callback. No file I/O, no `malloc`/`free`/`new`, no
   `std::string`/`std::vector` growth, no locks, no logging. Anything that
   touches disk or allocates goes through `reroll.cpp`'s worker as a command.
   (`create_instance` may call `rr_create`, which allocates and spawns the
   worker once. That's accepted, as in other catalog modules.)
2. **`get_param` must be cheap.** It runs on every UI repaint. Format cached
   data only, never scan.
3. **Never free a sample buffer on the audio thread.** Swap the pointer, put
   the old one in `retired[]`, and let `rr_collect_retired` hand it to the
   worker once no voice references it.
4. **Worker threads must demote first** (`SCHED_OTHER` plus cores 0–2) before
   doing anything else.
5. **Published folder lists are immutable** and live until `rr_destroy`. Don't
   reuse or free them early.
6. **Triggers:** option index 0 / `"-"` must be a no-op. Declare new ones in
   `kTriggers` with `access:"write"`.
7. **`module.json` < 8 KB.** Don't add per-pad defaults there; the DSP applies
   them.
8. **`help.json` lines ≤ 20 characters** (128×64 display). CI checks this.
9. **Only `move_plugin_init_v2` is exported.** Keep `-Wl,--version-script=scripts/exports.map`
   in every build. CI fails otherwise.
10. **Don't build with Ubuntu 24.04's `aarch64-linux-gnu-g++` for release.** It
    pulls in glibc 2.36/2.38 symbols. Use `build-zig.sh`, which pins 2.28.
11. **Max Len applies to rolls only.** A hand-picked sample always loads.
12. **Undo snapshots are pushed only when a roll actually changes a pad.**

## Adding a parameter

1. Persisted global: add to `kGlobalParams` in `mrdrums_params.cpp`, handle it
   in `set_param_value` / `get_param_value` (`mrdrums_plugin.cpp`), add a
   default to `module.json` `defaults`.
2. Per pad: add to `kPadFields`, handle the suffix in both functions, add a
   `pad_<x>` alias to `kCurrentPadAliases` if it should follow the current pad.
3. Trigger or readout (not persisted): handle in the `rr_` block of
   `set_param_value`, return a value in `get_param_value`, describe it in
   `build_chain_params_json`.
4. Put it on a page in **both** `build_ui_hierarchy` (live) and `module.json`
   (static fallback).
5. Add help lines, a test in `reroll_workflow_test.cpp`, and a line in
   `TASKS.md` / `CHANGELOG.md`.

## Testing expectations

- New engine behaviour → `reroll_engine_test.cpp`. New params or UI contract →
  `reroll_workflow_test.cpp`.
- Wait for async work with `settle()`. Never assume a load finished when
  `set_param` returns.
- Both sanitizers must pass, and run TSan more than once if you touched
  threading.
- Before a release, smoke-test the built `dist/reroll/dsp.so` (ARM64 under
  qemu, or on a Move).

## Style

- C++14, C-style where it touches the plugin API. Match the surrounding code.
- Comments explain *why*: realtime constraints, ownership, ordering.
- Keep `reroll.cpp` free of MrDrums types; it talks to the plugin through
  `rr_loader_fn` and `rr_ready_t` only.

## Upstream relationship

MrDrums is upstream (MIT, handcraftedcc). Keep `mrdrums_*` files close to
upstream so fixes can flow both ways. Reroll-only logic belongs in
`reroll.{h,cpp}` or clearly marked `/* ---- Reroll ---- */` blocks.
