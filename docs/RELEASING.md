# Putting FlukeDrums on GitHub and cutting releases

## 1. First-time setup: publish the repo

The folder is already a git repository with its history, so you only need to
publish it.

### With GitHub Desktop

1. **File → Add Local Repository…** and choose the `schwung-flukedrums` folder.
2. Click **Publish repository**.
   - Name: `schwung-flukedrums`
   - Untick **Keep this code private** if you want it public (needed for the
     Schwung catalog).
3. Done. The **Actions** tab on GitHub runs the tests on every push.

### Or with the command line

```bash
cd schwung-flukedrums
gh repo create schwung-flukedrums --public --source . --push
# or: create an empty repo on github.com, then
git remote add origin https://github.com/YOUR_GITHUB_USER/schwung-flukedrums.git
git push -u origin main
```

### After publishing

Replace `YOUR_GITHUB_USER` in `release.json` with your GitHub username and
commit that.

## 2. Cutting a release

Releases are built by GitHub Actions, so nothing needs to be built locally.

1. Bump the version in **both** `src/module.json` and `release.json`
   (including the `download_url`), e.g. `1.0.0` → `1.1.0`.
2. Add a section to `CHANGELOG.md`.
3. Commit, then create a **tag** `v1.1.0`:
   - GitHub Desktop: **History**, right-click the commit, **Create Tag…**,
     `v1.1.0`, then **Push origin** (Desktop pushes tags with it).
   - CLI: `git tag v1.1.0 && git push origin v1.1.0`
4. The **Release** workflow:
   - checks that the tag matches `module.json` and `release.json`,
   - builds `flukedrums-module.tar.gz` with Zig,
   - checks the binary needs glibc ≤ 2.31 and exports only one symbol,
   - creates a GitHub Release with the tarball attached.
5. Test the release tarball on your Move before announcing it.

## 3. Installing a release on a Move

Either way works:

- **Schwung Manager** (`http://move.local:7700`) → **Install Custom Module** →
  upload `flukedrums-module.tar.gz` from the release.
- **Schwung Manager** → **Install Custom Module** → paste the repo URL
  (`https://github.com/aTanguay/Schwung_FlukeDrums`). Schwung reads
  `release.json` to find the tarball.

## 4. Getting into the Schwung Module Store

The store is driven by
[`module-catalog.json`](https://github.com/charlesvestal/schwung/blob/main/module-catalog.json)
in the Schwung repo, served live from its `main` branch. Getting listed is a
one-time pull request that appends this entry to the end of the `modules`
array:

```json
    {
      "id": "flukedrums",
      "name": "FlukeDrums",
      "description": "A drum machine built on happy accidents: random kits from your sample library. Roll, lock the keepers, reroll the rest, save the kit",
      "author": "Andy Tanguay",
      "component_type": "sound_generator",
      "subcategory": "drum-machine",
      "tags": ["generative", "sample-based"],
      "github_repo": "aTanguay/Schwung_FlukeDrums",
      "default_branch": "main",
      "asset_name": "flukedrums-module.tar.gz",
      "min_host_version": "1.0.0"
    }
```

Schwung's CI validates every entry against `taxonomy.json`
(`tools/catalog/taxonomy.mjs`):

- `subcategory` must be one of the component type's ids. `drum-machine` is right.
- `tags` must come from the vocabulary, **sorted**, with no duplicates.
- **Don't add `drums`.** The `drum-machine` subcategory already implies it, and
  repeating an implied tag is an error.
- `needs-assets` is only allowed when a `requires` field says why. FlukeDrums
  works with Ableton's built-in library, so it has neither.
- `min_host_version`: 1.0.0 is the first Schwung with `access: "read"/"write"`
  (the readouts and the Roll / Undo / Save triggers). It's in the docs and code
  at v1.0.0 and absent at v0.12.1.

**After the first listing, releases need no PR.** Schwung Manager reads
`release.json` from this repo's `main`, so tagging `vX.Y.Z` (see section 2) is
all it takes. Only edit the catalog entry again to change the description,
tags or `min_host_version`.

### Opening the pull request (in the browser)

1. Open `module-catalog.json` on GitHub (link above) and click the pencil
   (**Edit**). GitHub offers to fork the repo for you; accept.
2. Go to the end of the `modules` array: after the last entry's closing `}`,
   add a comma, then paste the entry above. Commas are the usual mistake;
   GitHub's editor shows red marks if the JSON breaks.
3. **Commit changes… → Propose changes**, then **Create pull request**.
4. In the description, say what it is, link this repo, mention it's built on
   MrDrums, and **disclose the AI tools used (Claude)**, as Schwung's
   CONTRIBUTING.md asks.
5. Since this is your first contribution, Charles has to click
   **Approve and run** before CI runs. Once the checks are green he merges, and
   FlukeDrums shows up in Schwung Manager straight away.

Before submitting:

- [ ] Check that `release.json` on `main` points at a release that exists,
      with `flukedrums-module.tar.gz` attached (that's what the manager
      downloads)
- [ ] Read Schwung's
      [CONTRIBUTING.md](https://github.com/charlesvestal/schwung/blob/main/CONTRIBUTING.md).
      It asks you to disclose AI tools used (Claude) and does not accept
      contributions developed with Grok
- [ ] Consider talking to MrDrums' author first (TASKS.md, "Upstream &
      community"), so the catalog doesn't end up with two near-identical
      drum samplers without them knowing

## 5. Building locally (optional)

```bash
pip install ziglang
./scripts/build-zig.sh      # dist/flukedrums-module.tar.gz
./tests/run_tests.sh
```
