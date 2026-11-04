# v1.5.5 cut checklist

For the merge owner. Written 2026-11-04 on `leg/release` (cut from `launcher` at 3400733). Steps in order; each one
says where it runs. Mac commands are fish-compatible (the Mac's interactive shell is fish); Windows commands are Git
Bash unless marked PowerShell. Nothing reaches `main` or a tag until step 7, and nothing is uploaded until every
zip has passed step 5.

## 0. State at the time of writing

- `launcher` (local, main clone) is at bd3f869, one commit past origin/launcher 3400733: the guest clock reset, a
  candidate fix for the launcher-route access violation, "not proven to be the crash".
- Local `main` (b221c49, the v1.5.4 tag) is **behind origin/main** (a733b2c). origin/main carries 8a3a42b, b4aed57
  and two docs commits that are *cherry-picked copies* of launcher's 59db175 and 3400733 (2ebe3eb, a733b2c: the
  rc2/rc3 README + PORTS.md lines). `main` is therefore not an ancestor of `launcher` on origin, and merging
  `launcher` into `main` will conflict in README.md and PORTS.md exactly on the lines leg/release rewrote. Take
  `launcher`'s side for both (step 6).
- Three legs: `leg/macos` (wt-macos), `leg/polish` (wt-polish), `leg/release` (wt-release, this file). leg/release is
  docs and metadata only (and the build_app.sh VER default).
- Placeholders for the legs in flight: `git grep -n MERGE-OWNER` lists them (CHANGELOG.md three, README.md four,
  PORTS.md one). The changelog date is `2026-11-DD`.

## 1. Merge the legs into `launcher` (Mac, main clone)

```fish
cd ~/Desktop/projects/futari-mac/futari_recomp
git config user.email                     # must print 321120+springah@users.noreply.github.com
git switch launcher
git merge --no-ff leg/macos               # only if it landed
git merge --no-ff leg/polish              # only if it landed
git merge --no-ff leg/release
```

Expected conflicts: CHANGELOG.md, README.md, dist/RUN.txt if the macOS or polish leg wrote docs too. Keep
leg/release's structure and fold the other leg's facts into it.

Then resolve every placeholder **before building**, because `dist/RUN.txt` and `THIRD-PARTY-NOTICES.txt` ship in the
zips:

```fish
git grep -n MERGE-OWNER                   # edit each as its comment says, then delete the comment
git grep -n '2026-11-DD'                  # fill in the cut date (CHANGELOG heading)
```

What each placeholder depends on:

| leg | if it landed | if not |
|---|---|---|
| leg/macos | CHANGELOG bullet for macOS Renut; README Renderers last bullet + Known issues "Windows-only"; PORTS macOS row "Vulkan over MoltenVK, opt-in"; **also RUN.txt's macOS section** ("Renut is Windows-only for now", "the setup screen has no shader step"), which has no placeholder because RUN.txt ships | delete the comments; PORTS macOS row "not in this release (default renderer only)" |
| leg/polish | CHANGELOG bullet (gamepad navigation, font, Check for updates, shader package version stamp); delete the "no gamepad navigation" known limit in CHANGELOG, README and RUN.txt | delete the comments |
| access violation | CHANGELOG "Fix:" bullet; delete the README bullet and the CHANGELOG known limit | delete the comments; keep both limits. If bd3f869 ships unproven, say "parity fix" (see the CHANGELOG comment) |

Check that THIRD-PARTY-NOTICES.txt is current against the vendored SDK (a leg may have added a component):

```fish
bash scripts/gen_third_party_notices.sh > /tmp/tpn.txt; and cmp /tmp/tpn.txt THIRD-PARTY-NOTICES.txt
```

(The script needs the SDK's submodules checked out; a git worktree has none, run it in the main clone.)

Commit the resolution (`docs: v1.5.5 placeholders resolved`), check identity on everything new, and push `launcher`
so the Windows box can fetch it:

```fish
git log --format='%h %ae %ce' origin/launcher..launcher   # every line: both addresses 321120+springah@...
git push origin launcher
```

Record the commit (`git rev-parse launcher`): both zips are built from exactly this commit.

## 2. Decide before building: two open items

- **build.ps1 no longer builds a fresh clone.** It runs regen and configures `win-amd64-<config>`, but never runs
  `scripts/renut_shader_dump.sh`, and with `FUTARI_RENUT_STUB_SHADER_CACHE` OFF (the default since leg 4) the
  configure stops on the missing `out/renut_shader_dump/manifest.csv`. It also needs `pip install xxhash` and the
  dxc-bin binaries. The README's "Quick start" still says build.ps1 does everything. Either fix build.ps1 (a code
  change, not in this leg) or add a line to the README; the release build below does not depend on it.
- **What the macOS bundle links.** With no leg/macos, a plain `mac-arm64-release` configure links Renut with the
  game's shaders built in (game data) or fails without the dump. The public macOS bundle must come from a tree with
  `-DFUTARI_RENUT_RELEASE_SHADERS=ON` (if leg/macos made that work on macOS) or `-DFUTARI_RENUT_RENDERER=OFF`.
  If it is OFF, the macOS binaries do not link the GPL renderer; the notices then over-state (they name Renut),
  which is harmless, but the macOS RUN.txt LICENCES wording should say so if you want it exact.

## 3. Windows build (the Windows box, from the pushed `launcher`)

Prerequisites in that clone, never committed: `assets/` (the game data), the dxc-bin Windows binaries under
`thirdparty/reblue-XenosRecomp/thirdparty/dxc-bin/{bin,lib}/` (copy from the rc3 tree), `pip install xxhash`.

```bash
git fetch origin && git switch launcher && git merge --ff-only origin/launcher
git rev-parse HEAD                                  # == the commit recorded in step 1
git submodule update --init --recursive
```

From Git Bash started inside a "Developer PowerShell for VS 2022" (so `rc.exe` is on PATH), with the SDK CLI already
built (`build.ps1` builds it, or README step 2):

```bash
bash scripts/regen.sh                               # codegen from the game data
bash scripts/renut_shader_dump.sh                   # out/renut_shader_dump: the release table is made from it
cmake --preset win-amd64-release -B out/build/win-amd64-release-shaders -DFUTARI_RENUT_RELEASE_SHADERS=ON
cmake --build out/build/win-amd64-release-shaders   # no --target: the exe, the 8 DLLs and futari_renut_tools
grep REXSDK_DIR out/build/win-amd64-release-shaders/CMakeCache.txt          # the vendored tree
grep FUTARI_RENUT_RELEASE_SHADERS out/build/win-amd64-release-shaders/CMakeCache.txt   # BOOL=ON
ASSETS="$PWD/assets" bash scripts/stage_win_zip.sh 1.5.5
```

`stage_win_zip.sh` refuses a non-release tree, any `.exe` in `renut_tools/`, anything of the game in the stage, and
boot-checks the staged exe (Renut up with an empty table, the translator tool mode present, the default renderer
alive at 20 s). Output: `out/Mushihimesama-Futari-PC-v1.5.5.zip` + `.sha256`. `out/build/win-amd64-release` (the
build.ps1 tree) has the game shaders built in and is never zipped.

Hands check before going on: unzip the zip into an empty folder on a PC with Defender on, double-click `futari.exe`,
run the setup screen on one source, Play, F2, switch to Renut, Save, restart, check the log's `Renderer:` line.

## 4. macOS bundle (Mac, from the same commit)

```fish
cd ~/Desktop/projects/futari-mac/futari_recomp
git rev-parse HEAD                                  # == the commit recorded in step 1
bash scripts/renut_shader_dump.sh            # only if Renut is linked: the release table is made from the dump
cmake --preset mac-arm64-release -DFUTARI_RENUT_RELEASE_SHADERS=ON   # or -DFUTARI_RENUT_RENDERER=OFF, see step 2
grep FUTARI_RENUT out/build/mac-arm64-release/CMakeCache.txt
cmake --build --preset mac-arm64-release
set S ~/Desktop/projects/_stage/futari-v1.5.5-pub
rm -rf $S; mkdir -p $S/Mushihimesama-Futari-macOS/dlc
env VER=1.5.5 SKIP_ASSETS=1 APP=$S/Mushihimesama-Futari-macOS/Futari.app bash mac-build/build_app.sh
cp dist/RUN.txt scripts/extract_iso.py THIRD-PARTY-NOTICES.txt $S/Mushihimesama-Futari-macOS/
cp dist/dlc-README.txt $S/Mushihimesama-Futari-macOS/dlc/README.txt
find $S/Mushihimesama-Futari-macOS/Futari.app/Contents/MacOS -type f -exec file {} + | grep -v Mach-O   # must print nothing
cd $S; and ditto -c -k --keepParent --norsrc Mushihimesama-Futari-macOS Mushihimesama-Futari-macOS-v1.5.5.zip
rm -rf /tmp/futari-v155-check; mkdir /tmp/futari-v155-check
ditto -x -k Mushihimesama-Futari-macOS-v1.5.5.zip /tmp/futari-v155-check
codesign --verify --deep --strict /tmp/futari-v155-check/Mushihimesama-Futari-macOS/Futari.app; and echo seal OK
shasum -a 256 Mushihimesama-Futari-macOS-v1.5.5.zip > Mushihimesama-Futari-macOS-v1.5.5.zip.sha256
```

If the build ran Renut with a release table, also boot it once without a `vulkan_spirv.bin` and check the log says
there are no game shaders built in (the macOS equivalent of the Windows boot check). Smoke-boot the extracted app
with game data from a scratch copy, never from the stage.

## 5. Packaging rules 2 to 7 on each zip, BEFORE upload (PORTS.md "Releases")

Run on both zips (`$Z`). Old zips for the diff:

```fish
mkdir -p /tmp/v154; and gh release download v1.5.4 --repo springah/futari_recomp -p '*.zip' -D /tmp/v154
```

| rule | check | pass |
|---|---|---|
| 2 no game data | `unzip -Z1 $Z \| grep -iE '\.xex$\|\.bin$\|(^\|/)assets/\|shader_work\|shader_capture\|\.log$'` | prints nothing (no `vulkan_spirv.bin`, no logs) |
| 2 contents | `unzip -Z1 $Z` | Windows: exe, 8 `futari_*.dll`, `rexgpu-xenos.dll`, `rexruntime.dll`, `futari.ini`, `extract_assets.bat`, `extract_iso.py`, `make_shaders.py`, `renut_tools/` (4 files, no `.exe`), `RUN.txt`, `gamecontrollerdb.txt`, `THIRD-PARTY-NOTICES.txt`, `dlc/README.txt` |
| 2b licences | `unzip -p $Z '*THIRD-PARTY-NOTICES.txt' \| grep -c 'Version 3, 29 June 2007'` | 1 (the GPL-3.0 text); and `grep -c 'GPL-3.0 NOTICE AND SOURCE OFFER'` is 1 |
| 3 layout | `unzip -Z1 $Z \| cut -d/ -f1 \| sort -u` | one root: `Mushihimesama-Futari-PC` / `Mushihimesama-Futari-macOS`; no backslashes |
| 4 file-list diff | fish: `diff (unzip -Z1 /tmp/v154/Mushihimesama-Futari-PC-v1.5.4.zip \| sort \| psub) (unzip -Z1 $Z \| sort \| psub)` | Windows: only `make_shaders.py` and `renut_tools/*` added; macOS: whatever leg/macos added, nothing else |
| 6 no absolute paths | `unzip -p $Z '*futari.ini' \| grep -nE '[A-Za-z]:[\\/]\|/Users/\|/home/'` | prints nothing |
| 7 what it contains | `unzip` to a scratch dir, `strings futari.exe rexruntime.dll futari_*.dll \| grep -o 'rexglue-v[^ ]*' \| sort -u` (macOS: the binaries in Contents/MacOS and Frameworks) | one SDK version for every component |
| sha256 | `shasum -a 256 $Z` == the `.sha256` file | equal |

## 6. The docs restage commit, then `main` (Mac, main clone)

The v1.5.3 / v1.5.4 pattern (0e674e4, b221c49): one docs commit that dates the changelog entry and points the README
and PORTS.md at the release, and the tag sits on it or on its merge into `main`. leg/release already points README
and PORTS.md at v1.5.5, so what is left is anything the build taught you (and the date, if not filled in step 1):

```fish
git switch launcher
git commit -am "docs: v1.5.5 released with Windows and macOS bundles"    # only if anything changed
git fetch origin
git switch main; and git merge --ff-only origin/main     # local main is behind origin/main (a733b2c)
git merge --no-ff launcher -m "Merge launcher: v1.5.5 (Renut renderer opt-in, setup and settings screens)"
```

Conflicts in README.md and PORTS.md come from origin/main's cherry-picked rc2/rc3 docs commits (step 0): take
`launcher`'s version of both files, `git add` them, and finish the merge. Then:

```fish
git diff launcher main --stat                           # must print nothing: main's tree == launcher's
git log --format='%h %ae %ce' origin/main..main         # every author AND committer 321120+springah@...
```

The Guards identity job checks every pushed commit's author and committer, so the merge is done here, locally,
never with GitHub's merge button (which commits as GitHub).

## 7. Tag and push

```fish
git tag -a v1.5.5 -m "v1.5.5 -- Renut renderer opt-in, setup and settings screens in the exe; GPL-3.0 binaries" main
git merge-base --is-ancestor v1.5.5 main; and echo ancestor OK
git push origin main launcher v1.5.5
```

Watch the Guards run on `main` (no-game-data, shellcheck, identity) before the release.

## 8. GitHub Release

The notes body: the draft written with this checklist,
`/private/tmp/claude-501/-Users-shant-Desktop/906f8d28-efa8-421c-b9bd-d51aa24fa6ef/scratchpad/release_notes_v1.5.5.md`
(a temporary folder: copy it somewhere lasting first), with its MERGE-OWNER lines resolved the same way as step 1
and the two SHA-256 values filled in from the `.sha256` files. The tag message and title use `--`; earlier release
titles used an em-dash, switch if you prefer the match.

```fish
gh release create v1.5.5 --repo springah/futari_recomp --verify-tag \
  --title "v1.5.5 -- Renut renderer opt-in, setup screen in the exe; Windows + macOS" \
  --notes-file release_notes_v1.5.5.md \
  Mushihimesama-Futari-PC-v1.5.5.zip Mushihimesama-Futari-PC-v1.5.5.zip.sha256 \
  Mushihimesama-Futari-macOS-v1.5.5.zip Mushihimesama-Futari-macOS-v1.5.5.zip.sha256
```

A platform whose zip is not ready is left out and the notes say so; never attach an older zip. Afterwards, optionally
prepend a "Superseded by v1.5.5" line to the rc2 and rc3 notes, as rc1's has.

## 9. Replies owed

- **#8 (Scaloup, game speed):** v1.5.5 is out with Renut; the setup screen and F2 replace the rc1 steps (no
  `futari.ini` editing, no Python); ask the same question as the rc1 comment: does Renut feel like the 360, stage
  for stage against the default renderer, with the newest `logs\futari_*.log` and the GPU. Leave the issue open
  until they answer.
- **#4 (itemzer0, English menus):** v1.5.5 has a settings screen (F2) and a writer for top-level `futari.ini` lines
  (`futari_renderer`), which is where a language switch could live when the translation is ready, as discussed
  there. Mention that the translated map and injector are welcome, dumped strings still are not (game data).
  (This is my reading of what is owed on #4; check before posting.)

## 10. Clean up

```fish
git worktree remove ../wt-release; git worktree remove ../wt-polish; git worktree remove ../wt-macos
git branch -d leg/release leg/polish leg/macos
```
