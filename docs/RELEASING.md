# Putting Reroll on GitHub and cutting releases

## 1. First-time setup: publish the repo

The folder is already a git repository with one commit, so you only need to
publish it.

### With GitHub Desktop

1. **File → Add Local Repository…** and choose the `schwung-reroll` folder.
2. Click **Publish repository**.
   - Name: `schwung-reroll`
   - Untick **Keep this code private** if you want it public (needed for the
     Schwung catalog).
3. Done. The **Actions** tab on GitHub runs the tests on every push.

### Or with the command line

```bash
cd schwung-reroll
gh repo create schwung-reroll --public --source . --push
# or: create an empty repo on github.com, then
git remote add origin https://github.com/YOUR_GITHUB_USER/schwung-reroll.git
git push -u origin main
```

### After publishing

Replace `YOUR_GITHUB_USER` in `release.json` with your GitHub username and
commit that.

## 2. Cutting a release

Releases are built by GitHub Actions, so nothing needs to be built locally.

1. Bump the version in **both** `src/module.json` and `release.json`
   (including the `download_url`), e.g. `0.2.0` → `0.3.0`.
2. Add a section to `CHANGELOG.md`.
3. Commit, then create a **tag** `v0.3.0`:
   - GitHub Desktop: **History**, right-click the commit, **Create Tag…**,
     `v0.3.0`, then **Push origin** (Desktop pushes tags with it).
   - CLI: `git tag v0.3.0 && git push origin v0.3.0`
4. The **Release** workflow:
   - checks that the tag matches `module.json` and `release.json`,
   - builds `reroll-module.tar.gz` with Zig,
   - checks the binary needs glibc ≤ 2.31 and exports only one symbol,
   - creates a GitHub Release with the tarball attached.
5. Test the release tarball on your Move before announcing it.

## 3. Installing a release on a Move

Either way works:

- **Schwung Manager** (`http://move.local:7700`) → **Install Custom Module** →
  upload `reroll-module.tar.gz` from the release.
- **Schwung Manager** → **Install Custom Module** → paste the repo URL
  (`https://github.com/YOUR_GITHUB_USER/schwung-reroll`). Schwung reads
  `release.json` to find the tarball.

## 4. Getting into the Schwung Module Store

The store is driven by
[`module-catalog.json`](https://github.com/charlesvestal/schwung/blob/main/module-catalog.json)
in the Schwung repo. Open a pull request there that adds an entry like this:

```json
{
  "id": "reroll",
  "name": "Reroll",
  "description": "Random drum kits from your sample library: roll, lock the keepers, reroll the rest, save the kit",
  "author": "Andy Tanguay (engine: MrDrums by handcraftedcc)",
  "component_type": "sound_generator",
  "subcategory": "drum-machine",
  "tags": ["sample-based", "drums", "generative"],
  "github_repo": "YOUR_GITHUB_USER/schwung-reroll",
  "default_branch": "main",
  "asset_name": "reroll-module.tar.gz",
  "min_host_version": "1.0.0"
}
```

Before submitting:

- [ ] Confirm `min_host_version` on the oldest Schwung you can test (see
      TASKS.md)
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
./scripts/build-zig.sh      # dist/reroll-module.tar.gz
./tests/run_reroll_tests.sh
```
