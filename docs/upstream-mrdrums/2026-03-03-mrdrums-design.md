# mrdrums Design

## Goal
Build a new Schwung module named `mrdrums`: a 16-pad drum sampler with fast pad-centric editing, strict persisted parameter keys, and a semi-custom UI that dynamically binds controls to the currently selected pad.

## Scope
- Replace copied `granny` identity, docs, build/install packaging, metadata, and DSP/UI behavior with `mrdrums`.
- Keep only reusable infrastructure from the scaffold where useful (plugin v2 shell patterns, WAV loading/filepath support patterns, test harness approach).
- Deliver a playable v1 drum engine (not UI-only), while setting UI structure for end-goal workflow.

## Requirements Summary
- Top-level UI pages:
  - `Global`
  - `Pad Settings`
- `Pad Settings` edits the currently selected pad (1..16), not static pad submenus.
- On `Pad Settings`, pad note hits auto-switch current pad.
- Pad mapping for now: MIDI notes `36..51` => pads `1..16`.
- Focused parameter remains focused while switching pads.
- All sound/editable state persists through stable keys (`g_*`, `pXX_*`).
- `ui_current_pad` may be persisted for convenience (default `1` if absent).
- Sample selection uses filepath browser semantics, `.wav` only, writes `pXX_sample_path`.

## Persistence Model

### Global keys
- `g_master_vol`
- `g_polyphony`
- `g_vel_curve`
- `g_humanize_ms`
- `g_rand_seed`
- `g_rand_loop_steps`

### Per-pad keys (`p01..p16`)
- `pXX_sample_path`
- `pXX_vol`
- `pXX_pan`
- `pXX_tune`
- `pXX_start`
- `pXX_attack_ms`
- `pXX_decay_ms`
- `pXX_choke_group`
- `pXX_mode` (`gate` / `oneshot`)
- `pXX_rand_pan_amt`
- `pXX_rand_vol_amt`
- `pXX_rand_decay_amt`
- `pXX_chance_pct`

### UI-only optional key
- `ui_current_pad` (`1..16`)

Rules:
- DSP remains source of truth for all persisted keys.
- UI must read/write through module params only; no UI-local authoritative copies.
- Reload restores values from persisted keys exactly.

## Architecture

### DSP layer
- New drum-sampler engine replacing granular engine.
- 16 pad definitions with per-pad sample buffers and pad params.
- Voice allocator with global polyphony cap.
- Note-on behavior:
  - Map note `36..51` to pad.
  - Apply chance gate and randomizers using deterministic PRNG seeded from global seed + loop-step behavior.
  - Spawn voice with pad mode (`gate`/`oneshot`) and ADSR-like attack/decay behavior.
- Choke groups:
  - If pad has non-zero choke group, trigger cuts active voices in same group as specified.
- File loading:
  - `.wav` only for `pXX_sample_path`.
  - Missing files keep path but mark as unavailable at playback time.

### Param bridge
- `set_param`/`get_param` support for all keys above.
- `state` JSON returns/accepts all persisted values.
- `chain_params`/hierarchy metadata generated for host + Shadow UI integration.

### UI layer
- Custom `ui.js` for two-page flow (`Global`, `Pad Settings`).
- UI binding function computes active key by selected pad:
  - example: logical `Pan` => `p07_pan` when `ui_current_pad=7`.
- While on `Pad Settings`, note-on `36..51`:
  - update `ui_current_pad`
  - rebind displayed values to new `pXX_*`
  - preserve focused row/control.
- Sample entry opens filepath browser targeting active `pXX_sample_path`.

## Keep vs Remove from Copied Granny Code

### Keep (adapt as needed)
- Plugin API v2 instance lifecycle shell and host glue pattern.
- MIDI queue + thread-safe handoff approach.
- WAV parsing/loading utilities and basic path helpers.
- Existing filepath-browser shared JS tests/utilities.

### Remove/replace
- Granular engine (`granny_engine.*`) and all grain/scan/window/play-mode concepts.
- Granny-specific params, hierarchy pages, help text, metadata strings.
- NuSaw placeholder UI in `src/ui.js`.
- Granny naming in scripts/docs/release artifacts.

## Testing Plan
- DSP tests:
  - Persist/restore key roundtrip across globals + multiple pad keys.
  - Pad note mapping (`36..51`) correctness.
  - Choke behavior for grouped pads.
  - Gate vs oneshot release behavior.
  - Randomizer determinism with fixed seed/loop-steps.
- UI tests:
  - On `Pad Settings`, pad hits switch current pad and keep focus.
  - Dynamic key binding shows correct values when switching pads.
  - File selection writes to the correct `pXX_sample_path`.
- Build/package sanity:
  - Dist folder/module metadata reflect `mrdrums` naming and paths.

## Risks and Mitigations
- Risk: persisted key drift between UI and DSP.
  - Mitigation: central key helpers and roundtrip tests.
- Risk: copied scaffold artifacts leak old behavior.
  - Mitigation: explicit file audit and targeted rename/removal pass.
- Risk: missing sample handling can break pad UX.
  - Mitigation: keep missing path persisted; playback skip gracefully with clear status string.

## Acceptance Criteria
- Module identity everywhere is `mrdrums`.
- Module builds and installs under correct destination path/name.
- UI presents `Global` + `Pad Settings` with dynamic per-pad binding.
- Pad hits on `36..51` switch active edit pad on `Pad Settings` while focus remains unchanged.
- All `g_*` and `pXX_*` values persist and restore correctly.
- Module is playable as a drum sampler with sample-per-pad workflow.
