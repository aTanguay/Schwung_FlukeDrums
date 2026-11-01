# Tasks

Roadmap and to-dos. `[x]` done · `[ ]` open. Newest work goes at the top of
each section. See [PLANNING.md](PLANNING.md) for the design behind these.

## Now: refine after the first hardware sessions

- [ ] Hands-on review on the Move (Andy): note anything clunky in the
      roll → lock → roll loop
- [ ] Confirm no audio dropouts when rolling while Move tracks play, on a big
      library
- [ ] Measure the scan time for a large `My` library and decide whether Status
      needs a progress count
- [ ] Check that `Core` (`/data/CoreLibrary/Samples`) is the right path on
      current Move firmware, and whether Core has samples outside `Samples/`
- [ ] Decide the right default for **Max Len** (2 s today)
- [ ] Verify the minimum Schwung version (`access` / `options_as_string` /
      dynamic `chain_params`) and set `min_host_version`
- [ ] Check whether the Folder list refreshes on its own after the first scan,
      or only on re-entering the page (it shows `Scanning...` until then)

## Next: on the pads, not in menus

- [ ] **Pad gestures** via a `ui_chain.js` shim. Schwung routes Copy, Delete
      and Undo to a module's chain UI in component edit:
  - [ ] hold **Copy** + tap pad = toggle lock
  - [ ] hold **Delete** + tap pad = Roll Pad on that pad
  - [ ] **Undo** button = Undo Roll
  - [ ] light locked pads on the grid
- [ ] A one-button "Roll" on the first knob page (today Roll is a list item)
- [ ] Show the current pad's sample name in Status

## Later: more creative control

- [ ] **Save as a native Move kit** (`.ablpreset` in `UserLibrary/Track Presets`)
      pointing at the saved files. DR32 (legsmechanical, MIT) already has a
      `.ablpreset` writer in its `lib/`.
- [ ] **Stay in the family**: reroll a pad from the same folder as its
      current sample
- [ ] **Row filters**: tag pad rows by name ("kick", "snare", "hat") so each
      row rolls from matching files
- [ ] **Roll intensity**: reroll only N random unlocked pads
- [ ] **Seeded rolls**: a seed number so a roll can be shared and reproduced
- [ ] More library roots (e.g. a user-configurable extra folder via
      `settings-schema.json`)
- [ ] MP3/FLAC support (MrSample has decoders)

## Upstream & community

- [ ] Send the realtime fixes to MrDrums as a standalone PR: async sample
      loading + no allocation in `render_block`
- [ ] Ask handcraftedcc whether they want the roll-and-lock layer in MrDrums, or
      prefer FlukeDrums as its own module
- [ ] Submit FlukeDrums to the Schwung module catalog (see
      [docs/RELEASING.md](docs/RELEASING.md))
- [ ] Post on the Schwung Discord / Elektronauts thread with a short demo video

## Housekeeping

- [ ] Port `tests/upstream/` MrDrums tests to the async loader (add a settle
      step) and run them in CI
- [ ] Replace `YOUR_GITHUB_USER` in `release.json` once the repo exists
- [ ] Rename `mrdrums_*` sources once upstreaming is settled (keeping names
      for now makes merging upstream fixes easy)

## Done

- [x] v0.3.0: renamed **Reroll → FlukeDrums** (module id `flukedrums`); log lines now `[flukedrums]`
- [x] v0.2.0: **Folder picker** with counts (My / Core, 3 levels, 128 cap),
      **Up One**, Browse kept as fallback; coalesced pool changes when spinning
      the knob
- [x] v0.1.0 first hardware install worked
- [x] Build with Zig pinned to glibc 2.28; only the entry point exported;
      Docker no longer needed
- [x] Tests: engine + full plugin under ASan/UBSan and TSan, ARM64 under qemu,
      `dlopen` smoke test of the shipped binary
- [x] Roll / Roll Pad / Lock / Undo (32) / Save Kit / Rescan / Unlock All
- [x] Async worker: pool scan, decode, copy, free all off the audio thread;
      deferred free of buffers still playing
- [x] Fixed upstream realtime issues: WAV decode in `set_param`, vector
      allocation in `render_block`
- [x] Forked MrDrums, renamed to `reroll`, `module.json` under the 8 KB cap
