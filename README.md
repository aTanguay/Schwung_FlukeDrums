# FlukeDrums 

**A Schwung drum machine built on 'happy accidents'.**

FlukeDrums is a [Schwung](https://schwung.dev) module that fills Move's 16 pads with
random samples pulled from a folder you choose...including subfolders. Built from the excellent 'Mr Drums'

Like a pad? **Lock it.** Hit **Roll** again and only the unlocked pads change. Keep rolling, keep locking,
and when the kit sparks something, **Save Kit** copies it into your Samples
folder so Move's own browser can use it.

It's a happy-accident machine. You don't build a kit, you find one.


> **Status:** v0.4.1, early. It works on a real Move, but it's new.
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
- **Kit Mode**: the bottom two rows roll a real kit (kick, snare, clap,
  hats, cymbal, tom) found by sample names; the top two rows stay wild
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
3. **Knob 1 (Folder)**: pick where samples come from.
4. **Knob 4 (Roll)**: turn it, then play the pads.
5. Like a pad? Hit it, then turn **knob 5 (Lock)** clockwise.
6. **Roll** again. Repeat until it's good. **Knob 7** is Undo.
7. **Knob 8 (Save Kit)**. The kit shows up in Move's browser under `Samples/Schwung/FlukeDrums/`.

The first page's knobs read left to right as the steps, so a fresh session goes
1 → 8 and you rarely need to leave that page.

The [User Guide](docs/USER_GUIDE.md) covers every control, tips, and troubleshooting.

## Controls at a glance

Jog moves between five pages, in the order you'll use them:

**1. Roll**: the whole loop, left to right

| Knob | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| | Folder | Up One | Max Len | **Roll** | Lock | Roll Pad | Undo | Save Kit |
| | *choose the pool* | | | *roll* | *work the kit* | | | *keep it* |

On screen, knobs 1–4 are the top row and 5–8 the bottom row.

- **Folder**: where samples come from, from a list with sample counts. **Up One**: widen the pool a level.
- **Max Len**: skip samples longer than this many seconds (default 2, `0` = off).
- **Roll**: new random samples on every unlocked pad.
- **Lock**: clockwise locks the current pad, counter-clockwise unlocks it.
- **Roll Pad**: reroll just the current pad. **Undo**: put back what the last roll changed (32 deep).
  Pads you've locked or swapped by hand since stay put.
- **Save Kit**: copy the kit into your Samples folder.

Roll, Undo and Save Kit fire from a single turn of their knob (one flick = one press), or a click.

**2. Pad**: shape the pad you last hit. Vol, Pan, Tune, Start / Attack, Decay, Choke, Mode.

**3. Pad 2**: the pad's Sample (pick by hand), plus Rand Pan, Rand Vol, Rand Decay and Chance.

**4. Pool**: Kit Mode, Subfolders, Rescan, Browse (deeper folders), Unlock All, and readouts:
Pool Size, Kit Parts, Status (Undos and Last Kit on the page after).

**5. Global**: master volume, polyphony, velocity curve, humanize.

## Misc

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
