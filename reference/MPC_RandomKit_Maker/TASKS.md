# MPC Random Kit Maker — Tasks

## v1.0 (done)

- [x] Generate valid MPC .xpm (format v2.1) with 128 instrument slots
- [x] Recursive sample discovery (`.wav`, `.aif`, `.aiff`)
- [x] Random sample selection with `--seed` for reproducibility
- [x] Pretty-printed XML output with proper declaration
- [x] GM-style pad-to-MIDI-note mapping (pads 1-16)
- [x] CLI with `source_dir`, `--name`, `--count`, `--seed`

## v1.1 (done)

- [x] Default to script's own directory when no `source_dir` given
- [x] Self-contained output folder (`.xpm` + samples flat alongside it)
- [x] Copy samples with duplicate filename handling (`_2`, `_3`, etc.)
- [x] `os.scandir()` recursive walk for large library performance
- [x] Progress indicator during scanning (carriage-return update)
- [x] Progress indicator during sample copying
- [x] Skip existing `random_kit_*` output dirs during scan
- [x] Case-insensitive extension matching
- [x] Remove unused imports (`minidom`) and dead code (`pad_json_lines`)
- [x] Fix double-indentation bug
- [x] Move `datetime` import to top level, add `shutil`

## v1.2 (done)

- [x] MPC-compatible sample references (`<SampleName>` = stem, `<SampleFile>` = empty)
- [x] `<Layer>` elements include required `number` attribute (1-4)
- [x] No self-closing XML tags (MPC parser can't handle `<Tag />`; use `<Tag></Tag>`)
- [x] Empty layers use `<SliceEnd>0</SliceEnd>` (not `300000`)
- [x] Program-level AudioRoute set to `2` (Out 1/2 stereo main)
- [x] Skip macOS resource fork files (`._*`) during scanning
- [x] Use `shutil.copy` instead of `shutil.copy2` to avoid creating `._` files on exFAT/FAT32 volumes
- [x] Tested and confirmed working with MPC Desktop v3

## v1.3 (done)

- [x] Fun random kit names — `Adjective_Noun` combos from embedded word lists (125+ adjectives × 130+ nouns = 16,000+ combos)
- [x] Folder format: `RandomKit_{Name}_{YYYYMM}`, program/xpm: `{Name}`
- [x] Seed affects name generation (set before name pick for full reproducibility)
- [x] Skip both `random_kit_*` and `RandomKit_*` dirs during scan (backward compat)

## v1.4 (done)

- [x] Sample cache (`mpc_random_kit.cache`) — plain-text file next to the script with one path per line
- [x] Cache header stores source directory and date; auto-expires daily
- [x] Cache loads instantly on hit, full scan + write on miss
- [x] Graceful fallback — corrupt/missing cache just triggers a rescan

## v1.5 (done)

- [x] `--kits N` flag to generate multiple kits at once (1-10)
- [x] Interactive prompt when `--kits` not provided on command line
- [x] Each kit gets a unique random name and fresh sample selection
- [x] With `--name`, multi-kit appends `_1`, `_2`, etc.
- [x] Visual separators between kits when generating multiple

## v2.0 (future ideas)

### Smarter sample selection
- [ ] Category-aware selection — detect kick/snare/hat/etc. from filename or folder name and assign to appropriate pads
- [ ] `--pattern` flag to specify a layout (e.g., `--pattern "kick,snare,hat,hat"` fills pads 1-4 accordingly)
- [ ] Weight samples by folder depth or recency so "curated" top-level samples are preferred
- [ ] `--exclude` glob to skip certain folders or filename patterns

### Kit variations
- [x] ~~`--variations N`~~ → shipped as `--kits N` in v1.5
- [ ] `--swap N` to take an existing kit and randomly replace N pads
- [ ] `--layers` to fill multiple layers per pad (velocity layers)

### Audio analysis
- [ ] Read WAV headers to auto-set `SampleEnd` to actual sample length
- [ ] Detect sample rate / bit depth and warn on mismatches
- [ ] Optional normalization info in the `.xpm` (volume adjustment per pad)

### Output options
- [ ] `--zip` to output a `.zip` instead of a folder
- [ ] `--dry-run` to preview selections without copying anything
- [ ] Export pad assignments to a `.txt` or `.csv` manifest

### UX improvements
- [ ] Color terminal output (detect TTY, fall back to plain)
- [ ] `--quiet` flag to suppress progress output
- [ ] `--verbose` flag to show skipped dirs and permission errors
- [ ] Warn when `--count` exceeds available samples instead of silently capping

### Compatibility
- [ ] Support `.mp3`, `.flac`, `.ogg` (MPC software may accept these)
- [ ] Test against MPC Live / MPC One / MPC X firmware versions
- [ ] Generate MPC keygroup programs (`.xkp`) as an alternative to drum programs
