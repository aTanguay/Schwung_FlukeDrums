# FlukeDrums 🎲

**A drum machine built on lucky accidents, for Ableton Move.**

FlukeDrums is a [Schwung](https://schwung.dev) module that fills Move's 16 pads with
random samples pulled from a folder you choose. Like a pad? **Lock it.** Hit
**Roll** again and only the unlocked pads change. Keep rolling, keep locking,
and when the kit sparks something, **Save Kit** copies it into your Samples
folder so Move's own browser can use it.

It's a happy-accident machine. You don't build a kit, you find one.

```
 Roll  →  play  →  lock the keepers  →  Roll  →  …  →  Save Kit
```

> **Status:** v0.3.0, early. It works on a real Move, but it's new.
> Back up anything you care about before installing, and read Schwung's
> [important notice](https://github.com/charlesvestal/schwung#important-notice)
> (including DFU restore), as with any Schwung module.

---

## Features

- **Roll** all 16 pads, or just one, from a pool of thousands of samples
- **Lock** any pad so rolls leave it alone
- **Undo Roll**, 32 levels deep, for when you rolled away something good
- **Folder picker** with sample counts: your Samples (`My`) or Ableton's
  library (`Core`), up to three levels deep, with subfolders included
- **Max Len** filter keeps 4-minute stems out of your kick slot
- **Save Kit** writes the 16 files and a manifest of where each came from
- Everything [MrDrums](https://github.com/handcraftedcc/schwung-mrdrums) does:
  per-pad volume, pan, tune, start, envelope, choke groups, and loading Move
  `.ablpreset` kits
- Glitch-free: all disk work happens off the audio thread, so rolling while
  Move plays doesn't drop out

## Requirements

- Ableton Move with [Schwung](https://github.com/charlesvestal/schwung) installed
- Some WAV or AIFF samples on the Move. Ableton's built-in library counts.

## Install

1. Download `flukedrums-module.tar.gz` from the
   [latest release](../../releases/latest).
2. On a computer on the same network, open **http://move.local:7700**
   (Schwung Manager).
3. Choose **Install Custom Module** and upload the file.
4. Restart Schwung.

To update, do the same thing with the newer file. Your settings and locks are kept.

## Quick start

1. Load **FlukeDrums** into a Schwung slot, like any sound generator.
2. Point a Move track at it: hold **Shift**, press the **track button**, and
   set that track's MIDI out to the slot's channel.
3. Pick a **Folder** (on the first page, or turn knob 8).
4. Click **Roll** and play the pads.
5. Like a pad? Hit it, then turn **knob 1 (Lock)** to `on`.
6. **Roll** again. Repeat until it's good.
7. **Save Kit**. It shows up in Move's browser under `Samples/Schwung/FlukeDrums/`.

The [User Guide](docs/USER_GUIDE.md) covers every control, tips, and troubleshooting.

## Controls at a glance

| First page | |
|---|---|
| **Roll** | new random samples on every unlocked pad |
| **Undo Roll** | back to the kit before the last roll |
| **Lock** | lock or unlock the current pad (also **knob 1**) |
| **Roll Pad** | reroll just the current pad |
| **Save Kit** | copy the kit into your Samples folder |
| **Folder** | where samples come from (also **knob 8**) |
| **Status** | e.g. `Ready 4210 smp 3 lock` |

| Pool page | |
|---|---|
| **Folder** | pick the pool from a list with sample counts |
| **Up One** | widen the pool one folder level |
| **Subfolders** | include subfolders (default on) |
| **Max Len** | skip samples longer than this, in seconds (default 2, `0` = off) |
| **Rescan** | re-read the library after adding samples |
| **Browse** | pick a folder deeper than the list goes |
| **Unlock All** | clear every lock |

**Global** and **Pad Settings** work the same as in MrDrums.

## Building from source

You need Python 3, nothing else. No Docker.

```bash
pip install ziglang          # one-time: a cross-compiler for the Move's ARM chip
./scripts/build-zig.sh       # → dist/flukedrums-module.tar.gz
```

Then upload `dist/flukedrums-module.tar.gz` as in [Install](#install), or run
`./scripts/install.sh` to copy it over SSH.

To run the tests (macOS or Linux, needs a C++ compiler):

```bash
./tests/run_tests.sh
```

Developer docs:
- [PLANNING.md](PLANNING.md): how it works and why
- [TASKS.md](TASKS.md): roadmap
- [CLAUDE.md](CLAUDE.md): conventions and traps, for humans and AI agents
- [docs/RELEASING.md](docs/RELEASING.md): cutting a release

## Credits

- **FlukeDrums** by Andy Tanguay.
- Built on **[MrDrums](https://github.com/handcraftedcc/schwung-mrdrums)** by
  handcraftedcc (MIT). The sample engine, pad controls and `.ablpreset` loading are theirs.
- Runs on **[Schwung](https://github.com/charlesvestal/schwung)** by Charles Vestal.
- Developed with AI assistance (Claude). All code is reviewed and tested; see
  [PLANNING.md](PLANNING.md) for how.

Not affiliated with or endorsed by Ableton.

## About the name

A *fluke* is a lucky accident, which is the whole idea. It's also a nod to
W.S. "Fluke" Holland, Johnny Cash's drummer and one of rock and roll's first
drummers.

## License

[MIT](LICENSE)
