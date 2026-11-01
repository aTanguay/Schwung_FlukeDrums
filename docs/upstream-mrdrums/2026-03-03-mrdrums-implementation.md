# mrdrums Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Deliver a playable `mrdrums` 16-pad drum sampler module with persisted `g_*` and `pXX_*` keys plus dynamic pad-focused UI editing.

**Architecture:** Replace the copied granular engine with a pad-sampler DSP core and keep a thin API v2 plugin bridge for parameter/state I/O, sample loading, and rendering. Build a semi-custom UI with two pages (`Global`, `Pad Settings`) where bindings are computed from `ui_current_pad`, preserving focus across pad-switch note hits. Drive all behavior through persisted module params so save/load stays host-compatible.

**Tech Stack:** C++14 DSP plugin API v2, ES module UI JS, shell build/install scripts, Node test runner for JS tests, native C++ test binaries.

---

### Task 1: Rebrand module scaffold to `mrdrums`

**Files:**
- Modify: `src/module.json`
- Modify: `scripts/build.sh`
- Modify: `scripts/install.sh`
- Modify: `release.json`
- Modify: `README.md`
- Modify: `src/help.json`

**Step 1: Write the failing metadata check test**

Create `tests/module_identity.test.mjs`:

```javascript
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';

test('module identity is mrdrums', () => {
  const json = JSON.parse(fs.readFileSync('src/module.json', 'utf8'));
  assert.equal(json.id, 'mrdrums');
  assert.equal(json.name, 'mrdrums');
  assert.equal(json.abbrev, 'MRD');
});
```

**Step 2: Run test to verify it fails**

Run: `node --test tests/module_identity.test.mjs`
Expected: FAIL because copied granny metadata still exists.

**Step 3: Write minimal implementation**

Update metadata/script/doc strings:
- `granny-grain` -> `mrdrums`
- `Granny` -> `mrdrums`
- install destination -> `/data/UserData/schwung/modules/sound_generators/mrdrums`
- tarball -> `dist/mrdrums-module.tar.gz`

**Step 4: Run test to verify it passes**

Run: `node --test tests/module_identity.test.mjs`
Expected: PASS.

**Step 5: Commit**

```bash
git add tests/module_identity.test.mjs src/module.json scripts/build.sh scripts/install.sh release.json README.md src/help.json
git commit -m "chore: rebrand scaffold to mrdrums"
```

### Task 2: Add persisted key model for globals + per-pad params

**Files:**
- Create: `src/dsp/mrdrums_params.h`
- Create: `src/dsp/mrdrums_params.cpp`
- Create: `tests/mrdrums_params_keymap_test.cpp`
- Modify: `scripts/build.sh`

**Step 1: Write the failing keymap test**

Create `tests/mrdrums_params_keymap_test.cpp`:

```cpp
#include <cstdio>
#include <cstring>
#include "mrdrums_params.h"

int main() {
    char key[32];
    if (!mrdrums_make_pad_key(1, "pan", key, sizeof(key))) return 1;
    if (std::strcmp(key, "p01_pan") != 0) return 2;
    if (!mrdrums_make_pad_key(16, "chance_pct", key, sizeof(key))) return 3;
    if (std::strcmp(key, "p16_chance_pct") != 0) return 4;
    if (mrdrums_make_pad_key(0, "pan", key, sizeof(key))) return 5;
    std::printf("PASS: mrdrums pad key mapping\n");
    return 0;
}
```

**Step 2: Run test to verify it fails**

Run:
`g++ -std=c++14 -O0 -g tests/mrdrums_params_keymap_test.cpp src/dsp/mrdrums_params.cpp -Isrc/dsp -o /tmp/mrdrums_params_keymap_test && /tmp/mrdrums_params_keymap_test`
Expected: compile failure until new files are implemented.

**Step 3: Write minimal implementation**

Implement:
- pad index helpers (`1..16`)
- key formatter `p%02d_<suffix>`
- global key lookup table (`g_master_vol`, etc.)
- per-pad field descriptor table for all required `pXX_*` fields

**Step 4: Run test to verify it passes**

Run same command as Step 2.
Expected: `PASS: mrdrums pad key mapping`.

**Step 5: Commit**

```bash
git add src/dsp/mrdrums_params.h src/dsp/mrdrums_params.cpp tests/mrdrums_params_keymap_test.cpp scripts/build.sh
git commit -m "feat: add mrdrums persisted key model"
```

### Task 3: Implement playable drum engine core (notes, voices, choke, modes)

**Files:**
- Create: `src/dsp/mrdrums_engine.h`
- Create: `src/dsp/mrdrums_engine.cpp`
- Create: `tests/mrdrums_engine_note_map_test.cpp`
- Create: `tests/mrdrums_engine_choke_mode_test.cpp`

**Step 1: Write failing engine behavior tests**

`tests/mrdrums_engine_note_map_test.cpp` should assert:
- note 36 -> pad 1
- note 51 -> pad 16
- outside range invalid

`tests/mrdrums_engine_choke_mode_test.cpp` should assert:
- same choke group cuts previous active voice
- `oneshot` keeps sounding after note-off
- `gate` stops by envelope on note-off

**Step 2: Run tests to verify failures**

Run:
`g++ -std=c++14 -O0 -g tests/mrdrums_engine_note_map_test.cpp src/dsp/mrdrums_engine.cpp -Isrc/dsp -lm -o /tmp/mrdrums_engine_note_map_test && /tmp/mrdrums_engine_note_map_test`

Run:
`g++ -std=c++14 -O0 -g tests/mrdrums_engine_choke_mode_test.cpp src/dsp/mrdrums_engine.cpp -Isrc/dsp -lm -o /tmp/mrdrums_engine_choke_mode_test && /tmp/mrdrums_engine_choke_mode_test`

Expected: FAIL until engine exists.

**Step 3: Write minimal implementation**

Implement engine structs and logic:
- pad parameter storage (16 pads)
- note->pad mapper (`36..51`)
- fixed-size voice pool with polyphony cap
- per-voice playback cursor + attack/decay gain
- choke group cut on trigger
- gate/oneshot release behavior

**Step 4: Run tests to verify pass**

Run both commands again.
Expected: both tests PASS with printed PASS lines.

**Step 5: Commit**

```bash
git add src/dsp/mrdrums_engine.h src/dsp/mrdrums_engine.cpp tests/mrdrums_engine_note_map_test.cpp tests/mrdrums_engine_choke_mode_test.cpp
git commit -m "feat: add mrdrums playback engine"
```

### Task 4: Implement plugin v2 bridge with persisted state and per-pad sample paths

**Files:**
- Create: `src/dsp/mrdrums_plugin.cpp`
- Create: `tests/mrdrums_plugin_state_roundtrip_test.cpp`
- Create: `tests/mrdrums_plugin_sample_path_test.cpp`
- Modify: `scripts/build.sh`

**Step 1: Write failing plugin tests**

`tests/mrdrums_plugin_state_roundtrip_test.cpp`:
- create instance
- `set_param` multiple globals + multiple pad keys
- read `state` JSON
- create new instance from state
- verify restored values

`tests/mrdrums_plugin_sample_path_test.cpp`:
- set `p01_sample_path` to wav path
- verify `get_param("p01_sample_path")`
- verify missing path is preserved string-wise

**Step 2: Run tests to verify failures**

Run:
`g++ -std=c++14 -O0 -g tests/mrdrums_plugin_state_roundtrip_test.cpp src/dsp/mrdrums_plugin.cpp src/dsp/mrdrums_engine.cpp src/dsp/mrdrums_params.cpp -Isrc/dsp -lm -o /tmp/mrdrums_plugin_state_roundtrip_test && /tmp/mrdrums_plugin_state_roundtrip_test`

Run:
`g++ -std=c++14 -O0 -g tests/mrdrums_plugin_sample_path_test.cpp src/dsp/mrdrums_plugin.cpp src/dsp/mrdrums_engine.cpp src/dsp/mrdrums_params.cpp -Isrc/dsp -lm -o /tmp/mrdrums_plugin_sample_path_test && /tmp/mrdrums_plugin_sample_path_test`

Expected: FAIL until plugin implemented.

**Step 3: Write minimal implementation**

Implement:
- API v2 exports (`move_plugin_init_v2`)
- MIDI event queue to engine
- per-pad sample loading via `.wav` parser
- full param read/write for `g_*`, `pXX_*`, `ui_current_pad`
- `state` and `chain_params`/`ui_hierarchy` generation for required keys

**Step 4: Run tests to verify pass**

Run both commands from Step 2.
Expected: PASS.

**Step 5: Commit**

```bash
git add src/dsp/mrdrums_plugin.cpp tests/mrdrums_plugin_state_roundtrip_test.cpp tests/mrdrums_plugin_sample_path_test.cpp scripts/build.sh
git commit -m "feat: add mrdrums plugin api bridge"
```

### Task 5: Build semi-custom UI with dynamic current-pad bindings

**Files:**
- Replace: `src/ui.js`
- Create: `tests/mrdrums_ui_binding.test.mjs`
- Create: `tests/mrdrums_ui_pad_switch.test.mjs`

**Step 1: Write failing UI tests**

`tests/mrdrums_ui_binding.test.mjs` should assert:
- `activeKey('pan', 1)` => `p01_pan`
- `activeKey('pan', 12)` => `p12_pan`

`tests/mrdrums_ui_pad_switch.test.mjs` should assert:
- while in `Pad Settings`, note 36..51 updates `ui_current_pad`
- focused row id remains unchanged after pad switch

**Step 2: Run tests to verify failures**

Run: `node --test tests/mrdrums_ui_binding.test.mjs tests/mrdrums_ui_pad_switch.test.mjs`
Expected: FAIL until UI helpers exist.

**Step 3: Write minimal implementation**

Implement in `src/ui.js`:
- two pages: `Global`, `Pad Settings`
- active key resolver by `ui_current_pad`
- input handler for note-on `36..51`
- focus-preservation logic
- sample browser action writes to active `pXX_sample_path`

**Step 4: Run tests to verify pass**

Run same command as Step 2.
Expected: PASS.

**Step 5: Commit**

```bash
git add src/ui.js tests/mrdrums_ui_binding.test.mjs tests/mrdrums_ui_pad_switch.test.mjs
git commit -m "feat: add mrdrums dynamic pad settings ui"
```

### Task 6: Final verification, package validation, and cleanup

**Files:**
- Modify: `src/help.json`
- Modify: `README.md`
- Delete: `src/dsp/granny_engine.cpp`
- Delete: `src/dsp/granny_engine.h`
- Delete: `src/dsp/granny_plugin.cpp`
- Modify: `tests/*` (rename/remove obsolete granny tests)

**Step 1: Write failing integration smoke test**

Create `tests/mrdrums_release_layout.test.mjs`:

```javascript
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';

test('dist package contains mrdrums artifacts', () => {
  assert.equal(fs.existsSync('dist/mrdrums/module.json'), true);
  assert.equal(fs.existsSync('dist/mrdrums/dsp.so'), true);
  const mod = JSON.parse(fs.readFileSync('dist/mrdrums/module.json', 'utf8'));
  assert.equal(mod.id, 'mrdrums');
});
```

**Step 2: Run build + tests to verify initial failure**

Run: `./scripts/build.sh && node --test tests/mrdrums_release_layout.test.mjs`
Expected: FAIL before final packaging/docs cleanup is complete.

**Step 3: Write minimal implementation**

- remove obsolete granny sources/tests from build and tree
- update docs/help for mrdrums workflow
- ensure dist output path is `dist/mrdrums`

**Step 4: Run full verification to pass**

Run:
- `node --test tests/*.test.mjs`
- `./scripts/build.sh`
- `node --test tests/mrdrums_release_layout.test.mjs`

Expected: all PASS.

**Step 5: Commit**

```bash
git add README.md src/help.json scripts/build.sh tests src

git commit -m "feat: finish mrdrums module and remove granny leftovers"
```
