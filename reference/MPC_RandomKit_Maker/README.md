# MPC Random Kit Maker

Generate random Akai MPC drum programs (`.xpm`) from your sample library. Drop the script next to your samples, run it, get a self-contained kit folder ready for MPC software or hardware.

**Zero dependencies.** Python 3.6+ standard library only.

## Quick start

```bash
# From your samples folder — it asks how many kits you want
python3 mpc_random_kit.py

# Or point it at a folder
python3 mpc_random_kit.py /path/to/samples

# Generate 5 kits at once
python3 mpc_random_kit.py --kits 5

# Reproducible kit with a custom name
python3 mpc_random_kit.py --seed 42 --name "My Kit"
```

Each kit gets a fun random name and its own self-contained folder:

```
RandomKit_Molten_Anvil_202603/
├── Molten_Anvil.xpm
├── kick.wav
├── snare.wav
└── ...
```

## Options

| Flag | Default | Description |
|---|---|---|
| `source_dir` | Script's own directory | Where to scan for samples |
| `--name NAME` | Random `Adjective_Noun` combo | Kit name |
| `--count N` | 64 | Number of pads to fill (max 128) |
| `--seed N` | Random | Seed for reproducibility |
| `--kits N` | Asks interactively | Number of kits to generate (1-10) |

## Supported formats

`.wav`, `.aif`, `.aiff` (case-insensitive)

## How it works

1. Checks for a cached sample index (`mpc_random_kit.cache`) — loads instantly on cache hit, full scan on miss
2. Recursively scans the source directory for audio files (if no cache)
3. Asks how many kits you want (or use `--kits N`)
4. For each kit: picks a random name, selects `--count` unique samples, copies them into the kit folder, and generates a valid MPC `.xpm` drum program (format v2.1) routed to Out 1/2

Works well with large sample libraries (tested with 300K+ samples) — uses fast `os.scandir()` traversal, caches sample paths for instant subsequent runs, shows progress, and automatically skips macOS resource fork files (`._*`).

## Tips

- **Inspiration tool**: crank out a batch of kits, load them up, and flip through random combinations you'd never pick manually
- **Reproducible kits**: use `--seed` to share or recreate exact kits (affects both name and sample selection)
- **Safe to re-run**: previous output folders (`RandomKit_*` / `random_kit_*`) are automatically skipped during scanning
- **Cache**: the sample index (`mpc_random_kit.cache`) rebuilds automatically each day or when you change source directories — delete it to force a rescan
- **MPC compatibility**: tested with MPC Desktop v3; the `.xpm` format is the same across MPC Live, One, X, and the desktop software
- **External drives**: works on exFAT/FAT32 volumes without creating macOS `._` resource fork files

## License

MIT
