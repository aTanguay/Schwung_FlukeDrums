"""Core kit-building logic for MPC RandoKitter.

Shared by the CLI (mpc_random_kit.py) and the GUI (mpc_randokitter/gui.py).
Zero third-party dependencies — Python 3.6+ stdlib only.
"""

import datetime
import io
import os
import random
import re
import shutil
import threading
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Callable, List, Optional

AUDIO_EXTENSIONS = {".wav", ".aif", ".aiff"}

# Word pools for random kit names (Adjective_Noun combos)
_ADJECTIVES = (
    "Fuzzy", "Crispy", "Dusty", "Velvet", "Golden", "Cosmic", "Hazy", "Neon",
    "Rusty", "Silky", "Gritty", "Dreamy", "Burnt", "Frozen", "Liquid", "Atomic",
    "Lazy", "Electric", "Smoky", "Crystal", "Copper", "Mellow", "Savage",
    "Bitter", "Heavy", "Loose", "Warm", "Raw", "Deep", "Dark", "Bright",
    "Swift", "Wild", "Bold", "Strange", "Ancient", "Broken", "Hidden", "Hollow",
    "Faded", "Tangled", "Twisted", "Sunken", "Molten", "Woven", "Phantom",
    "Sonic", "Primal", "Bleached", "Sticky", "Wonky", "Murky", "Lush",
    "Polar", "Solar", "Lunar", "Gentle", "Fierce", "Sacred", "Wicked",
    "Mossy", "Peppered", "Charred", "Gilded", "Warped", "Grainy", "Oily",
    "Chunky", "Salty", "Smoked", "Brittle", "Cloudy", "Steamy", "Dense",
    "Wooly", "Spicy", "Muted", "Vivid", "Glassy", "Sandy", "Muddy",
    "Frayed", "Coiled", "Dented", "Crushed", "Soaked", "Bruised", "Peeled",
    "Scratched", "Rippled", "Layered", "Painted", "Waxed", "Frosted", "Salted",
    "Roasted", "Cracked", "Polished", "Rugged", "Jagged", "Smooth", "Coarse",
    "Dim", "Faint", "Thick", "Thin", "Flat", "Sharp", "Blunt",
    "Quiet", "Loud", "Slow", "Quick", "Tiny", "Grand", "Humble",
    "Crooked", "Slanted", "Curved", "Steep", "Narrow", "Vast", "Stray",
    "Hallow", "Ashen", "Brazen", "Sullen", "Serene", "Somber", "Lucid",
)

_NOUNS = (
    "Cream", "Thunder", "Pulse", "Groove", "Haze", "Bloom", "Vinyl", "Smoke",
    "Echo", "Ember", "Grain", "Tide", "Dust", "Honey", "Ghost", "Moss",
    "Rust", "Storm", "Dusk", "Flame", "Frost", "Amber", "Slate", "Silk",
    "Gravel", "Chrome", "Vapor", "Drift", "Bone", "Ash", "Coral", "Loom",
    "Spark", "Glow", "Shade", "Void", "Peak", "Root", "Sway", "Crunch",
    "Snap", "Thump", "Knock", "Buzz", "Rumble", "Drip", "Sizzle", "Howl",
    "Glitch", "Orbit", "Jungle", "Velour", "Concrete", "Marble", "Canvas",
    "Cipher", "Prism", "Horizon", "Mirage", "Oasis",
    "Plume", "Splinter", "Cobalt", "Tundra", "Anvil", "Furnace", "Lantern",
    "Thistle", "Bramble", "Fjord", "Canyon", "Ridge", "Crater", "Quarry",
    "Timber", "Flint", "Copper", "Pewter", "Burlap", "Linen", "Tweed",
    "Mortar", "Ratchet", "Piston", "Cinder", "Sulfur", "Magma", "Obsidian",
    "Quartz", "Basalt", "Granite", "Lichen", "Thorn", "Nettle", "Clover",
    "Birch", "Cedar", "Willow", "Sage", "Thyme", "Fennel", "Pepper",
    "Saffron", "Cacao", "Sesame", "Marrow", "Rind", "Husk", "Pith",
    "Sinew", "Gristle", "Tallow", "Resin", "Pitch", "Lacquer", "Patina",
    "Verge", "Brink", "Ledge", "Ravine", "Gorge", "Shoal", "Marsh",
    "Steppe", "Mesa", "Dune", "Bluff", "Cairn", "Spire", "Turret",
)

def generate_fun_name():
    """Pick a random Adjective_Noun combo for the kit."""
    return f"{random.choice(_ADJECTIVES)}_{random.choice(_NOUNS)}"

# Standard GM-ish pad → MIDI note layout for 128 pads.
# Pads 1-16 follow a typical MPC bank A drum layout, rest fill upward.
def default_pad_notes(count=128):
    base = [
        37, 36, 42, 82,   # pad 1-4:  side stick, kick, closed HH, ???
        40, 38, 46, 44,   # pad 5-8:  snare, snare2, open HH, pedal HH
        48, 47, 45, 43,   # pad 9-12: hi tom, mid tom, low tom, floor tom
        49, 55, 51, 53,   # pad 13-16: crash, ride bell, ride, ride2
    ]
    notes = []
    for i in range(count):
        if i < len(base):
            notes.append(base[i])
        else:
            # Fill remaining pads chromatically from note 60 upward
            notes.append(60 + (i - len(base)))
    return notes

# ---------------------------------------------------------------------------
# Sample cache
# ---------------------------------------------------------------------------

CACHE_FILENAME = "mpc_random_kit.cache"

def load_cache(source_dir):
    """Try to load sample paths from the cache file.

    Returns a list of paths if the cache is fresh (created today, same source
    directory). Returns None if the cache is missing, stale, or corrupt.

    The cache file lives inside the source directory so it travels with the
    sample library.
    """
    cp = os.path.join(os.path.abspath(source_dir), CACHE_FILENAME)
    try:
        with open(cp, "r", encoding="utf-8") as f:
            lines = f.read().splitlines()
    except (OSError, UnicodeDecodeError):
        return None

    # Need at least the 3 header lines
    if len(lines) < 3:
        return None

    # Parse header
    try:
        cached_source = lines[1].split("# source: ", 1)[1]
        cached_date = lines[2].split("# date: ", 1)[1]
    except (IndexError, ValueError):
        return None

    # Check source dir and date
    today = datetime.date.today().isoformat()
    if cached_source != os.path.abspath(source_dir) or cached_date != today:
        return None

    # Remaining lines are sample paths (skip blanks)
    samples = [line for line in lines[3:] if line]
    return samples if samples else None

def write_cache(source_dir, samples):
    """Write sample paths to the cache file with metadata header.

    The cache file lives inside the source directory.
    """
    cp = os.path.join(os.path.abspath(source_dir), CACHE_FILENAME)
    today = datetime.date.today().isoformat()
    try:
        with open(cp, "w", encoding="utf-8") as f:
            f.write("# mpc_random_kit cache — delete this file to force a rescan\n")
            f.write(f"# source: {os.path.abspath(source_dir)}\n")
            f.write(f"# date: {today}\n")
            for path in samples:
                f.write(path + "\n")
    except OSError:
        pass  # Non-fatal — next run will just rescan

# ---------------------------------------------------------------------------
# Sample discovery
# ---------------------------------------------------------------------------

def find_samples(root_dir, skip_dirs=None, progress_cb: Optional[Callable[[int], None]] = None):
    """Recursively find all audio files under root_dir using os.scandir().

    Prints a running count (or calls progress_cb if supplied) so the caller
    knows scanning is progressing. Skips directories in skip_dirs (absolute
    paths) to avoid picking up previously generated kits.
    """
    samples = []
    last_count = [0]
    skip_set = set(skip_dirs) if skip_dirs else set()

    def _print_progress():
        count = len(samples)
        if count - last_count[0] >= 500 or count == 0:
            if progress_cb:
                progress_cb(count)
            else:
                print(f"\r  Scanning... found {count:,} samples so far", end="", flush=True)
            last_count[0] = count

    def _walk(path):
        try:
            with os.scandir(path) as it:
                for entry in it:
                    if entry.is_dir(follow_symlinks=False):
                        if os.path.abspath(entry.path) in skip_set:
                            continue
                        _walk(entry.path)
                    elif entry.is_file(follow_symlinks=False):
                        # Skip macOS resource fork files (._*)
                        if entry.name.startswith("._"):
                            continue
                        if Path(entry.name).suffix.lower() in AUDIO_EXTENSIONS:
                            samples.append(entry.path)
                            _print_progress()
        except PermissionError:
            pass

    _walk(root_dir)
    # Clear the progress line (only in CLI mode)
    if progress_cb is None:
        print(f"\r  Found {len(samples):,} samples.{' ' * 20}")
    return samples

# ---------------------------------------------------------------------------
# XPM generation
# ---------------------------------------------------------------------------

def indent_xml(elem, level=0):
    """Add pretty-print indentation to an ElementTree in place."""
    pad = "\n" + "  " * level
    if len(elem):
        if not elem.text or not elem.text.strip():
            elem.text = pad + "  "
        if not elem.tail or not elem.tail.strip():
            elem.tail = pad
        for child in elem:
            indent_xml(child, level + 1)
        if not child.tail or not child.tail.strip():
            child.tail = pad
    else:
        if level and (not elem.tail or not elem.tail.strip()):
            elem.tail = pad
    if not level:
        elem.tail = "\n"

def make_audio_route(route=0):
    ar = ET.Element("AudioRoute")
    ET.SubElement(ar, "AudioRoute").text = str(route)
    ET.SubElement(ar, "AudioRouteSubIndex").text = "0"
    ET.SubElement(ar, "AudioRouteChannelBitmap").text = "3"
    ET.SubElement(ar, "InsertsEnabled").text = "True"
    return ar

def make_lfo():
    lfo = ET.Element("LFO")
    ET.SubElement(lfo, "Type").text = "Sine"
    ET.SubElement(lfo, "Rate").text = "0.500000"
    ET.SubElement(lfo, "Sync").text = "0"
    ET.SubElement(lfo, "Reset").text = "False"
    return lfo

def make_layer(number, sample_path=None):
    """Build a single Layer element. number is 1-4. If sample_path is given, populate it."""
    layer = ET.Element("Layer", number=str(number))
    ET.SubElement(layer, "Active").text = "True"
    ET.SubElement(layer, "Volume").text = "1.000000"
    ET.SubElement(layer, "Pan").text = "0.500000"
    ET.SubElement(layer, "Pitch").text = "0.000000"
    ET.SubElement(layer, "TuneCoarse").text = "0"
    ET.SubElement(layer, "TuneFine").text = "0"
    ET.SubElement(layer, "VelStart").text = "0"
    ET.SubElement(layer, "VelEnd").text = "127"
    ET.SubElement(layer, "SampleStart").text = "0"
    ET.SubElement(layer, "SampleEnd").text = "0"
    ET.SubElement(layer, "Loop").text = "False"
    ET.SubElement(layer, "LoopStart").text = "0"
    ET.SubElement(layer, "LoopEnd").text = "0"
    ET.SubElement(layer, "LoopCrossfadeLength").text = "0"
    ET.SubElement(layer, "LoopTune").text = "0"
    ET.SubElement(layer, "Mute").text = "False"
    ET.SubElement(layer, "RootNote").text = "0"
    ET.SubElement(layer, "KeyTrack").text = "False"

    if sample_path:
        # MPC resolves samples by SampleName (stem only, no extension).
        # SampleFile is left empty — MPC finds the file in the same directory.
        ET.SubElement(layer, "SampleName").text = Path(sample_path).stem
        ET.SubElement(layer, "SampleFile").text = ""
        slice_end = "300000"
    else:
        ET.SubElement(layer, "SampleName").text = ""
        ET.SubElement(layer, "SampleFile").text = ""
        slice_end = "0"

    ET.SubElement(layer, "SliceIndex").text = "129"
    ET.SubElement(layer, "Direction").text = "0"
    ET.SubElement(layer, "Offset").text = "0"
    ET.SubElement(layer, "SliceStart").text = "0"
    ET.SubElement(layer, "SliceEnd").text = slice_end
    ET.SubElement(layer, "SliceLoopStart").text = "0"
    ET.SubElement(layer, "SliceLoop").text = "0"
    ET.SubElement(layer, "SliceLoopCrossFadeLength").text = "0"
    return layer

def make_instrument(number, sample_path=None):
    inst = ET.Element("Instrument", number=str(number))

    inst.append(make_audio_route())

    for send in ("Send1", "Send2", "Send3", "Send4"):
        ET.SubElement(inst, send).text = "0.000000"

    ET.SubElement(inst, "Volume").text = "0.707946"
    ET.SubElement(inst, "Mute").text = "False"
    ET.SubElement(inst, "Solo").text = "False"
    ET.SubElement(inst, "Pan").text = "0.500000"
    ET.SubElement(inst, "AutomationFilter").text = "1"
    ET.SubElement(inst, "TuneCoarse").text = "0"
    ET.SubElement(inst, "TuneFine").text = "0"
    ET.SubElement(inst, "Mono").text = "True"
    ET.SubElement(inst, "Polyphony").text = "1"
    ET.SubElement(inst, "FilterKeytrack").text = "0.000000"
    ET.SubElement(inst, "LowNote").text = "0"
    ET.SubElement(inst, "HighNote").text = "127"
    ET.SubElement(inst, "IgnoreBaseNote").text = "False"
    ET.SubElement(inst, "ZonePlay").text = "1"
    ET.SubElement(inst, "MuteGroup").text = "0"
    for mt in ("MuteTarget1","MuteTarget2","MuteTarget3","MuteTarget4"):
        ET.SubElement(inst, mt).text = "0"
    for st in ("SimultTarget1","SimultTarget2","SimultTarget3","SimultTarget4"):
        ET.SubElement(inst, st).text = "0"
    ET.SubElement(inst, "LfoPitch").text = "0.000000"
    ET.SubElement(inst, "LfoCutoff").text = "0.000000"
    ET.SubElement(inst, "LfoVolume").text = "0.000000"
    ET.SubElement(inst, "LfoPan").text = "0.000000"
    ET.SubElement(inst, "OneShot").text = "True"
    ET.SubElement(inst, "FilterType").text = "2"
    ET.SubElement(inst, "Cutoff").text = "1.000000"
    ET.SubElement(inst, "Resonance").text = "0.000000"
    ET.SubElement(inst, "FilterEnvAmt").text = "0.000000"
    ET.SubElement(inst, "AfterTouchToFilter").text = "0.000000"
    ET.SubElement(inst, "VelocityToStart").text = "0.000000"
    ET.SubElement(inst, "VelocityToFilterAttack").text = "0.000000"
    ET.SubElement(inst, "VelocityToFilter").text = "0.000000"
    ET.SubElement(inst, "VelocityToFilterEnvelope").text = "0.000000"
    ET.SubElement(inst, "FilterAttack").text = "0.000000"
    ET.SubElement(inst, "FilterDecay").text = "0.047244"
    ET.SubElement(inst, "FilterSustain").text = "1.000000"
    ET.SubElement(inst, "FilterRelease").text = "0.000000"
    ET.SubElement(inst, "FilterHold").text = "0.000000"
    ET.SubElement(inst, "FilterDecayType").text = "True"
    ET.SubElement(inst, "FilterADEnvelope").text = "True"
    ET.SubElement(inst, "VolumeHold").text = "0.000000"
    ET.SubElement(inst, "VolumeDecayType").text = "True"
    ET.SubElement(inst, "VolumeADEnvelope").text = "True"
    ET.SubElement(inst, "VolumeAttack").text = "0.000000"
    ET.SubElement(inst, "VolumeDecay").text = "0.047244"
    ET.SubElement(inst, "VolumeSustain").text = "1.000000"
    ET.SubElement(inst, "VolumeRelease").text = "0.000000"
    ET.SubElement(inst, "VelocityToPitch").text = "0.000000"
    ET.SubElement(inst, "VelocityToVolumeAttack").text = "0.000000"
    ET.SubElement(inst, "VelocitySensitivity").text = "1.000000"
    ET.SubElement(inst, "VelocityToPan").text = "0.000000"
    inst.append(make_lfo())
    ET.SubElement(inst, "WarpTempo").text = "120.000000"
    ET.SubElement(inst, "BpmLock").text = "True"
    ET.SubElement(inst, "WarpEnable").text = "False"
    ET.SubElement(inst, "StretchPercentage").text = "100"

    layers_el = ET.SubElement(inst, "Layers")
    # Layer 1 gets the sample; layers 2-4 are empty stubs
    layers_el.append(make_layer(1, sample_path))
    for n in range(2, 5):
        layers_el.append(make_layer(n, None))

    return inst

def make_pad_note_map(pad_notes):
    pm = ET.Element("PadNoteMap")
    for i, note in enumerate(pad_notes, 1):
        pn = ET.SubElement(pm, "PadNote", number=str(i))
        ET.SubElement(pn, "Note").text = str(note)
    return pm

def make_pad_group_map(count=128):
    pgm = ET.Element("PadGroupMap")
    for i in range(1, count + 1):
        pg = ET.SubElement(pgm, "PadGroup", number=str(i))
        ET.SubElement(pg, "Group").text = "0"
    return pgm

def generate_xpm(kit_name, selected_samples, pad_count=64):
    """Build the full MPCVObject XML tree."""
    root = ET.Element("MPCVObject")

    # Version block
    ver = ET.SubElement(root, "Version")
    ET.SubElement(ver, "File_Version").text = "2.1"
    ET.SubElement(ver, "Application").text = "MPC-V"
    ET.SubElement(ver, "Application_Version").text = "2.6.0.16"
    ET.SubElement(ver, "Platform").text = "OSX"

    # Program block
    prog = ET.SubElement(root, "Program", type="Drum")
    ET.SubElement(prog, "ProgramName").text = kit_name

    # ProgramPads JSON blob (MPC expects this; use a safe minimal version)
    ET.SubElement(prog, "ProgramPads").text = "{\n    " + '"ProgramPads": {}\n}'

    prog.append(make_audio_route(route=2))  # 2 = Out 1/2 (stereo main)

    for send in ("Send1", "Send2", "Send3", "Send4"):
        ET.SubElement(prog, send).text = "0.000000"

    ET.SubElement(prog, "Volume").text = "0.707946"
    ET.SubElement(prog, "Mute").text = "False"
    ET.SubElement(prog, "Solo").text = "False"
    ET.SubElement(prog, "Pan").text = "0.500000"
    ET.SubElement(prog, "AutomationFilter").text = "1"
    ET.SubElement(prog, "Pitch").text = "0.000000"
    ET.SubElement(prog, "TuneCoarse").text = "0"
    ET.SubElement(prog, "TuneFine").text = "0"
    ET.SubElement(prog, "Mono").text = "False"
    ET.SubElement(prog, "Program_Polyphony").text = "128"

    # Instruments: 128 total slots, first pad_count get samples
    instruments_el = ET.SubElement(prog, "Instruments")
    for i in range(1, 129):
        sample = selected_samples[i - 1] if i <= len(selected_samples) else None
        instruments_el.append(make_instrument(i, sample))

    # Pad note map (128 pads)
    prog.append(make_pad_note_map(default_pad_notes(128)))

    # Pad group map
    prog.append(make_pad_group_map(128))

    # QLinkAssignments (empty, required by MPC)
    ET.SubElement(prog, "QLinkAssignments")

    return root

# ---------------------------------------------------------------------------
# Self-contained kit output
# ---------------------------------------------------------------------------

def copy_samples_to_kit(selected_samples, out_dir, progress_cb: Optional[Callable[[int, int], None]] = None):
    """Copy selected samples into out_dir alongside the .xpm.

    Handles duplicate filenames by appending _2, _3, etc.
    Returns a list of destination filenames (e.g. 'kick.wav') in the same
    order as selected_samples.

    When progress_cb is set, it is called as progress_cb(i, total) per file
    instead of printing to the terminal.
    """
    dest_names = []
    used_names = {}  # stem -> count

    for i, src in enumerate(selected_samples, 1):
        stem = Path(src).stem
        suffix = Path(src).suffix

        # Handle duplicate filenames
        if stem in used_names:
            used_names[stem] += 1
            dest_name = f"{stem}_{used_names[stem]}{suffix}"
        else:
            used_names[stem] = 1
            dest_name = f"{stem}{suffix}"

        dest = os.path.join(out_dir, dest_name)
        shutil.copy(src, dest)
        dest_names.append(dest_name)

        if progress_cb:
            progress_cb(i, len(selected_samples))
        else:
            print(f"\r  Copying samples... {i}/{len(selected_samples)}", end="", flush=True)

    if progress_cb is None:
        print()  # newline after progress
    return dest_names

# ---------------------------------------------------------------------------
# XPM file writer
# ---------------------------------------------------------------------------

def write_xpm_file(root: ET.Element, xpm_path: str) -> None:
    """Pretty-print an XPM tree and write to disk with MPC-compatible tags."""
    tree = ET.ElementTree(root)
    try:
        ET.indent(tree, space="  ")
    except AttributeError:
        indent_xml(root)

    buf = io.BytesIO()
    buf.write(b'<?xml version="1.0" encoding="UTF-8"?>\n\n')
    tree.write(buf, encoding="utf-8", xml_declaration=False)
    xml_str = buf.getvalue().decode("utf-8")
    xml_str = re.sub(r'<(\w+)\s*/>', r'<\1></\1>', xml_str)

    with open(xpm_path, "w", encoding="utf-8") as f:
        f.write(xml_str)

# ---------------------------------------------------------------------------
# Kit generation (single + orchestrator)
# ---------------------------------------------------------------------------

def generate_kit(
    samples: List[str],
    out_root: str,
    kit_name: Optional[str] = None,
    count: int = 64,
    copy_progress_cb: Optional[Callable[[int, int], None]] = None,
) -> str:
    """Build one kit folder. Returns absolute path to the folder created.

    `samples` is the pool to select from; `out_root` is the parent dir under
    which the kit's folder will be created. `kit_name` of None = random.
    """
    count = min(count, 128, len(samples))
    if kit_name is None:
        kit_name = generate_fun_name()
        ts = datetime.datetime.now().strftime("%Y%m")
        folder_name = f"RandomKit_{kit_name}_{ts}"
    else:
        folder_name = kit_name

    out_dir = os.path.join(os.path.abspath(out_root), folder_name)
    os.makedirs(out_dir, exist_ok=True)

    selected = random.sample(samples, count)
    dest_names = copy_samples_to_kit(selected, out_dir, progress_cb=copy_progress_cb)

    root = generate_xpm(kit_name, dest_names, pad_count=count)
    write_xpm_file(root, os.path.join(out_dir, f"{kit_name}.xpm"))
    return out_dir


def generate_kits(
    source_dir: str,
    output_dir: str,
    num_kits: int,
    pads_per_kit: int = 64,
    name_override: Optional[str] = None,
    seed: Optional[int] = None,
    progress_cb: Optional[Callable[[float, str], None]] = None,
    cancel_event: Optional[threading.Event] = None,
) -> List[str]:
    """Full pipeline: cache/scan samples, generate N kits. Returns kit paths.

    `progress_cb(fraction, status_text)` is called for UI updates; `fraction`
    is 0.0-1.0, `status_text` is a human-readable phase message. Passing
    `cancel_event` allows cooperative cancellation between kits.
    """
    def report(fraction: float, status: str) -> None:
        if progress_cb:
            progress_cb(fraction, status)

    if seed is not None:
        random.seed(seed)

    source_dir = os.path.abspath(source_dir)
    output_dir = os.path.abspath(output_dir)

    report(0.0, "Loading sample index…")
    samples = load_cache(source_dir)
    if not samples:
        skip_dirs = []
        try:
            for entry in os.scandir(source_dir):
                if entry.is_dir() and (entry.name.startswith("random_kit_")
                                       or entry.name.startswith("RandomKit_")):
                    skip_dirs.append(os.path.abspath(entry.path))
        except PermissionError:
            pass

        def scan_progress(count: int) -> None:
            report(0.4, f"Scanning samples… {count:,} found")

        samples = find_samples(source_dir, skip_dirs=skip_dirs,
                               progress_cb=scan_progress)
        if not samples:
            raise RuntimeError(f"No .wav/.aif/.aiff files found under {source_dir}")
        write_cache(source_dir, samples)

    report(0.7, f"Found {len(samples):,} samples — generating kits…")

    os.makedirs(output_dir, exist_ok=True)
    kits = []
    for kit_num in range(1, num_kits + 1):
        if cancel_event is not None and cancel_event.is_set():
            break

        if name_override:
            kit_name = name_override if num_kits == 1 else f"{name_override}_{kit_num}"
        else:
            kit_name = None  # generate_kit picks a random one

        display_name = kit_name or "(random)"
        report(0.7 + 0.3 * (kit_num - 1) / num_kits,
               f"Generating kit {kit_num} of {num_kits}: {display_name}")

        kit_path = generate_kit(samples, output_dir, kit_name=kit_name,
                                count=pads_per_kit)
        kits.append(kit_path)

    report(1.0, f"Done — {len(kits)} kit(s) in {output_dir}")
    return kits
