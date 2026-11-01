# FlukeDrums User Guide

- [The idea](#the-idea)
- [Setup](#setup)
- [The pages](#the-pages)
- [Choosing where samples come from](#choosing-where-samples-come-from)
- [Rolling, locking, undo](#rolling-locking-undo)
- [Saving a kit](#saving-a-kit)
- [Shaping the sounds](#shaping-the-sounds)
- [Tips and tricks](#tips-and-tricks)
- [Troubleshooting](#troubleshooting)
- [Where files go](#where-files-go)

---

## The idea

FlukeDrums puts a random sample on each of Move's 16 pads. You play, keep what
works, and reroll the rest. Locked pads survive every roll, so a kit builds up
one keeper at a time. Nothing is destructive: your samples are only read,
**Undo Roll** walks back 32 rolls, and **Save Kit** copies files instead of
moving them.

## Setup

1. **Install.** Upload `flukedrums-module.tar.gz` at **http://move.local:7700** →
   **Install Custom Module**, then restart Schwung.
2. **Load it.** Open Schwung, pick an empty slot, and choose **FlukeDrums** from
   the sound generators.
3. **Connect a Move track.** Hold **Shift** and press the track button you want
   to play FlukeDrums from, then set that track's **MIDI out** channel to match the
   Schwung slot (slot A listens on channel 1 by default, B on 2, and so on).
   Now Move's pads and sequencer play FlukeDrums.

   Use an empty drum or audio track for this. Whatever instrument the Move track
   has will also play underneath.
4. **Check Status** on the **Pool** page. After a moment it should say
   something like `Ready 4210 smp 0 lock`: ready, 4210 samples in the pool,
   no pads locked.

Pads are silent until your first roll.

## The pages

Jog moves between five pages, ordered the way you'll use them:

| # | Page | Top row (knobs 1–4) | Bottom row (knobs 5–8) |
|---|---|---|---|
| 1 | **Roll** | Folder · Up One · Max Len · Roll | Lock · Roll Pad · Undo · Save Kit |
| 2 | **Pad** | Vol · Pan · Tune · Start | Attack · Decay · Choke · Mode |
| 3 | **Pad 2** | Sample · Rand Pan · Rand Vol · Rand Decay | Chance |
| 4 | **Pool** | Subfolders · Rescan · Browse · Unlock All | Pool Size · Undos · Status · Last Kit |
| 5 | **Global** | Master Vol · Polyphony · Vel Curve · Humanize | Rand Seed · Rand Loop |

Page 1 holds the whole loop, and its knobs read left to right as the steps
(Move's eight knobs are one physical row): **choose the pool** (1–3), **Roll**
(4), **work the kit** (5–7), **Save Kit** (8). Most of the time you never leave
it. Pages 2 and 3 follow whichever pad you last hit. The bottom row of Pool is
read-only info (drawn with a dotted frame).

If you prefer a scrolling list to the knob grid, Schwung's **Global Settings →
Display → Param View** switches to List. The order is the same.

## Choosing where samples come from

The **pool** is the folder FlukeDrums draws from, plus everything below it.

### Folder (the main way)

**Folder** is knob 1 on the first page. It lists your sample folders
with how many samples each holds, counting subfolders:

```
My (4210)              ← everything in your Samples folder
My/Drums (1980)
My/Drums/Kicks (412)
My/Vox (96)
Core (2300)            ← Ableton's built-in library
Core/Drums (900)
```

- **My** is your Samples folder (`UserLibrary/Samples`): everything you've
  recorded or uploaded through Move Manager.
- **Core** is Ableton's Core Library that ships on the Move.

Turn knob 1 to step through folders, or hold it and click for the full list.
The pool changes as soon as you land on one.

The list shows folders up to three levels deep and at most 128 of them (a
Schwung limit). With a big library, shallower folders get priority.

### Up One

**Up One** (knob 2) widens the pool to the folder above. Start narrow on
`My/Drums/Kicks`, lock a kick, hit **Up One** twice, and now the other pads
roll from your whole library. It stops at the top of My or Core.

### Browse

For a folder too deep for the list, use **Browse** on the Pool page. Schwung's
browser can only pick *files*, so open the folder you want and **click any
sample inside it**. FlukeDrums uses that file's folder. **Folder** then shows it as
`Custom: <name>`.

### Subfolders and Max Len

- **Subfolders** (default `on`): include everything below the chosen folder.
  Turn it off to use only the files directly inside it.
- **Max Len** (default `2.0` s): skip samples longer than this when rolling, so
  loops, stems and full songs don't land on a pad. Set `0` for no limit. Raise
  it if you want long textures, or lower it for tight one-shots.
  It only affects rolls. A sample you pick by hand always loads.

### Rescan

Added samples through Move Manager? Hit **Rescan** on the Pool page to update
the pool and the folder list.

## Rolling, locking, undo

| Action | What it does |
|---|---|
| **Roll** | New random sample on every **unlocked** pad. Picks are unique within a roll when the pool allows. |
| **Roll Pad** | Reroll only the current pad (unless it's locked). |
| **Lock** / knob 5 | Clockwise locks the current pad, counter-clockwise unlocks it. Locked pads are never touched by Roll. |
| **Undo Roll** | Restore the kit from before the last roll or Roll Pad. Up to 32 steps. |
| **Unlock All** | Clear every lock (Pool page). |

**Which pad is "current"?** With **Auto Select** on (the default), the last
pad you hit. So the flow is: hit a pad, hear it, turn knob 5.

On the first page, **Roll** (knob 4), **Roll Pad** (6), **Undo** (7) and
**Save Kit** (8) fire from a single turn of their knob: one flick is one press,
so spinning it won't roll ten times. Lock sits right next to Roll because
that's the loop (roll, hear it, lock it), and Undo is kept three knobs away so a
bumped Roll knob can't take it out.

Rolls load in the background. Pads update within a fraction of a second, and
you can keep playing while they do. A pad that's still ringing finishes on its
old sample.

## Saving a kit

**Save Kit** copies the current 16 samples into a new folder:

```
Samples/Schwung/FlukeDrums/Fluke 2026-10-31 231045/
    01 Kick_Dusty.wav
    02 Snap_04.wav
    …
    16 Vox_Chop_12.wav
    flukedrums-kit.json        ← which original file each pad came from
```

Status shows `Saved: Fluke 2026-…`. The folder appears in Move's own sample
browser, so you can build a native Move drum kit from it. Saved kits are never
pulled back into FlukeDrums's pool.

Your Schwung set also remembers the kit, the locks, and the pool, so you don't
have to Save Kit just to keep working tomorrow.

## Shaping the sounds

Everything from MrDrums is here, per pad, on the **Pad** page (page 2) and
**Pad 2** (page 3). They follow the pad you last hit:

- **Vol, Pan, Tune** (±24 semitones), **Start** (with waveform view)
- **Attack, Decay**, **Mode** (`oneshot` or `gate`)
- **Choke** groups (open and closed hats on the same group, for example)
- **Rand Pan / Vol / Decay** and **Chance** for per-hit variation
- **Global**: master volume, polyphony, velocity curve, humanize

These settings stay with the pad when its sample rerolls. Tune a pad down an
octave and every sample that lands there plays down an octave.

## Tips and tricks

- **Narrow, then widen.** Start on one folder (Kicks), lock a great one, then
  **Up One** and let the rest of the library fight over the other pads.
- **Mix libraries.** Roll from **Core** for a while, lock a few, then switch
  Folder to **My** and roll the rest. Ableton's sounds next to your own
  recordings make for weird, good combos.
- **Tune the random ones.** Leave a few pads pitched way down or up; whatever
  lands there becomes something new.
- **One pad at a time.** Lock everything you like, then use **Roll Pad** on the
  one slot that still isn't right.
- **Max Len as a texture knob.** `0.5` s gives tight hits; `6` s lets in
  swells, risers and vocal phrases.
- **Choke for drama.** Put two random pads on one choke group and play them
  against each other.

## Troubleshooting

**Status says `No samples`.** The pool folder has no WAV or AIFF files (or
none short enough). Pick another **Folder**, check **Subfolders** is on, or set
**Max Len** to `0`.

**Folder says `Scanning...`.** The folder list is built right after FlukeDrums
loads, which takes a second or two on a big library. Back out of the page and
come back in. If it persists, hit **Rescan**.

**A folder I want isn't in the list.** It's deeper than three levels, or the
list hit 128 entries. Use **Browse** (pick any file in the folder), or pick its
parent and keep Subfolders on.

**Roll does nothing.** Are all pads locked? Check Status for the lock count, or
hit **Unlock All**. Rolls also skip files longer than **Max Len**.

**No sound.** Check the Move track's MIDI out channel matches the Schwung slot,
and that you've rolled at least once. Pads map to notes 36–51.

**Audio crackles while rolling.** Please
[open an issue](../../../issues) with your library size, what else was
running, and your Schwung version. It shouldn't happen.

**Getting a log.** Schwung can write a debug log to
`/data/UserData/schwung/debug.log` (see Schwung's
[LOGGING.md](https://github.com/charlesvestal/schwung/blob/main/docs/LOGGING.md)).
Attach it to an issue.

## Where files go

| What | Where on the Move |
|---|---|
| The module | `/data/UserData/schwung/modules/sound_generators/flukedrums/` |
| Saved kits | `/data/UserData/UserLibrary/Samples/Schwung/FlukeDrums/` |
| "My" in the folder list | `/data/UserData/UserLibrary/Samples` |
| "Core" in the folder list | `/data/CoreLibrary/Samples` |

FlukeDrums only ever *writes* inside `Samples/Schwung/FlukeDrums/`. Everything else is
read-only to it.
