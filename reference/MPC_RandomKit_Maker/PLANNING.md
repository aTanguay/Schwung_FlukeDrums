# MPC Random Kit Maker — Planning

## What it does

A zero-config Python script that builds random Akai MPC drum programs (`.xpm` files) from a directory of audio samples. Drop the script next to your samples, run it, get a self-contained kit folder ready to load into MPC software or hardware.

## Design goals

1. **Zero dependencies** — Python 3.6+ standard library only, no pip installs
2. **Zero config** — works with no arguments; scans the script's own directory by default
3. **Self-contained output** — each kit is a folder with the `.xpm` + copied samples, so you can move/share it freely
4. **Large library friendly** — uses `os.scandir()` for fast recursive traversal, shows progress, skips its own output directories
5. **Reproducible** — `--seed` flag lets you recreate the exact same kit

## Architecture

```
mpc_random_kit.py          # single-file script, no modules
```

### Flow

1. **Parse args** — optional `source_dir`, `--name`, `--count`, `--seed`, `--kits`
2. **Interactive prompt** — if `--kits` wasn't given on the command line, ask the user how many kits (1-10, default 1)
3. **Cache check** — look for `mpc_random_kit.cache` next to the script; if it exists, matches today's date and the same source dir, load paths instantly. Otherwise do a full scan and write the cache for next time
4. **Scan (if cache miss)** — recursive `os.scandir()` walk, case-insensitive extension matching (`.wav`, `.aif`, `.aiff`), skips existing `random_kit_*` / `RandomKit_*` output dirs
5. **Kit loop** — for each kit (1 to N):
   a. **Name** — if no `--name`, pick a random `Adjective_Noun` combo from embedded word lists (125+ adjectives × 130+ nouns = 16,000+ combos); folder = `RandomKit_{name}_{YYYYMM}`, program/xpm = `{name}`
   b. **Select** — `random.sample()` picks N unique samples
   c. **Copy** — samples copied flat alongside the `.xpm` in the kit folder, duplicates get `_2`, `_3` suffixes
   d. **Generate XML** — builds the full MPC `.xpm` (format v2.1) with 128 instrument slots, pad-note map, pad-group map; `<SampleName>` is the stem (no extension), `<SampleFile>` is empty (MPC resolves by name)
   e. **Write** — pretty-printed XML with `<?xml?>` declaration, self-closing tags expanded to `<Tag></Tag>` (MPC requirement)

### Output structure

```
RandomKit_Fuzzy_Cream_202603/
├── Fuzzy_Cream.xpm
├── kick.wav
├── snare.wav
└── ...
```

### XPM format notes

- Root element: `<MPCVObject>`
- 128 instrument slots (pads), each with 4 numbered layers (`<Layer number="1">` through `4`); layer 1 = sample, 2-4 = empty stubs
- `<SampleFile>` is empty — MPC resolves samples by `<SampleName>` (stem, no extension) from the same directory as the `.xpm`
- Program-level `<AudioRoute>` must be `2` (Out 1/2 stereo main); instrument-level stays `0` (follow program)
- No self-closing XML tags allowed — MPC's parser rejects `<Tag />`; must use `<Tag></Tag>`
- Empty layers use `<SliceEnd>0</SliceEnd>`; layers with samples use `300000`
- `<ProgramPads>` contains a minimal JSON blob (MPC requires it but ignores contents for basic kits)
- Pad-to-MIDI-note mapping follows GM drum layout for pads 1-16, then chromatic from note 60

## Decisions made

| Decision | Reasoning |
|---|---|
| Single file, no package | Maximizes portability — users just download one `.py` |
| `os.scandir()` over `os.walk()` | Faster on large trees, gives us `DirEntry` objects to check type without extra stat calls |
| Copy samples instead of symlinking | Symlinks break when moving the kit folder; copying makes it truly portable |
| Samples flat alongside `.xpm`, not in subfolder | MPC resolves by `<SampleName>` and expects files next to the `.xpm` |
| `<SampleFile>` left empty | MPC ignores it and uses `<SampleName>` to find files; confirmed from working kits |
| Skip `random_kit_*` / `RandomKit_*` dirs during scan | Prevents picking up samples from previous runs when script lives in the sample directory |
| Fun random names (`Adjective_Noun`) over timestamps | More memorable, fun to browse; embedded word lists keep the single-file design |
| Seed set before name generation | With `--seed`, both the name and sample picks are reproducible |
| No `--out` flag | Output path is deterministic from the kit name; one less thing to think about |
| Duplicate filename handling with `_2` suffix | Simple, predictable, preserves the original stem for readability |
| Program AudioRoute = `2`, instruments = `0` | `2` routes to Out 1/2; `0` means "follow program". Confirmed from working MPC kits |
| No self-closing XML tags | MPC's XML parser chokes on `<Tag />`; regex post-pass expands to `<Tag></Tag>` |
| Skip `._*` files during scan | macOS resource forks match audio extensions but aren't real samples |
| `shutil.copy` not `copy2` | Avoids creating `._` resource fork files when copying to exFAT/FAT32 external drives |
| Plain-text cache next to script | One path per line, 3-line header with source dir + date; human-readable, instant to load vs. re-scanning 300K+ files |
| Cache expires daily | Simple staleness check — compare `# date:` to today. Delete the file to force a rescan |
| Cache next to script, not source dir | Keeps portability simple — one script, one cache file. Rebuilds automatically if source dir changes |
| Interactive `--kits` prompt | If not passed on CLI, ask the user at runtime — friendlier than requiring a flag |
| Clamp kits to 1-10 | Keeps output manageable; 10 kits × 64 samples = 640 files max |
| Multi-kit with `--name` appends `_1`, `_2` | Predictable, avoids collisions; single-kit keeps the clean name |
