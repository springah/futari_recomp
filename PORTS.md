# Mushihimesama Futari — Ports

Per-OS build and release notes. Build prerequisites and the Windows walkthrough
live in the [README](README.md); version history is in [CHANGELOG.md](CHANGELOG.md).

## Platforms

| OS | Build | Status |
|----|-------|--------|
| Windows (x64) | `win-amd64-release` preset (clang-cl), or `build.ps1` → `out/build/win-amd64-release` | ✅ v1.5.2 (SDK v0.9.3.1) |
| Steam Deck / Linux (x64) | `linux-amd64-release` preset (any x64 Linux, WSL included) | ✅ v1.5.2 (boot-verified on device) |
| macOS (arm64) | `mac-arm64-release` preset on an Apple Silicon Mac, bundled by `mac-build/build_app.sh` | ✅ v1.5.2 (MoltenVK) |

**Windows builds use the `win-amd64-*` presets**, which name clang-cl from
`C:/Program Files/LLVM` (the SDK's own presets still default to `clang++`, so the
SDK CLI is configured by hand; `build.ps1` does both). Configure from a VS
developer shell so `rc.exe` is found. Build with no `--target`, or with the build
preset, so the 8 module DLLs get the same optimisation flags as the exe. The
`win-arm64-*` presets target `arm64-pc-windows-msvc` and are untested.

## SDK

All builds link the `thirdparty/rexglue-sdk` tree, which is **vendored** — this
port owns its SDK and takes fixes directly in-tree (see
[thirdparty/rexglue-sdk/VENDORED.md](thirdparty/rexglue-sdk/VENDORED.md) for
provenance: fork tag v0.9.3.1 and the documented deviations). Binaries stamp the
version from the SDK's `REXGLUE_VERSION` file. `-DREXSDK_DIR=<path>` overrides
the vendored tree with a separate checkout.

The vendored tree descends from springah/rexglue-sdk, a fork of
[rexglue/rexglue-sdk](https://github.com/rexglue/rexglue-sdk), and carries the
clang-cl build fixes and the macOS fixes this port depends on; two of the latter
are written up in [docs/upstream/](docs/upstream/) and have not been offered
upstream yet. Pointing `REXSDK_DIR` at an upstream checkout will miss them —
expect the macOS path in particular to fail there.

There is no pin to bump. To take an SDK change, apply it to the tree and commit
it like any other change; when pulling a batch from the fork or upstream, diff
against the recorded provenance tag and update `REXGLUE_VERSION` + VENDORED.md.

⚠️ Check what a build directory is pinned to before shipping from it —
`grep REXSDK_DIR out/build/<dir>/CMakeCache.txt`. A directory configured with an
explicit `-DREXSDK_DIR` keeps pointing at that checkout, not the vendored tree,
until it is reconfigured.

**macOS needs Homebrew LLVM.** The SDK's compiler gate rejects AppleClang by both
ID and version, so the `mac-*` presets point at `/opt/homebrew/opt/llvm/bin/clang++`.
MoltenVK is `dlopen`ed at runtime rather than vendored — `brew install molten-vk`
to build and run from a build directory; `mac-build/build_app.sh` bundles a copy
into the `.app` so the shipped bundle needs no Homebrew.

## Releases

One SemVer tag per release; per-OS zips are named
`Mushihimesama-Futari-PC-vX.Y.Z.zip`, `-Deck-`, `-macOS-`. No game content goes
into a bundle or a tag, ever.

Each tag gets a **GitHub Release with the per-OS zips attached** (from v1.5.3
on; earlier bundles went only to a private download share). The release notes
are the changelog entry people can link to. A release asset is public the moment
it is uploaded, so rules 2–7 below run on a zip *before* it is attached, never
after. A platform whose bundle is not restaged yet is simply absent from that
release; say so in the notes rather than linking an older zip.

A tag must be an ancestor of `main`. If history is ever rewritten, re-point every
tag at its counterpart commit and confirm the trees match before pushing:
`git merge-base --is-ancestor <tag> main` is the check, and a release cut from an
orphaned tag silently publishes a tree that is on no branch.

One known exception: `main` begins at v1.3.0 (a 2026-08 history rewrite dropped
the commits before it), so `v1.0.0`–`v1.1.0` point into history that is on no
branch and have no counterpart to re-point at. They stay for the record; nothing
is released from them. Everything from v1.3.1 on is reachable from `main`. Note
for any future rewrite: GitHub's `refs/pull/N/head` refs keep the old objects
fetchable and only GitHub Support can delete them, so a scrub that must be
complete means a fresh repository, not another rewrite.

Packaging rules — each one is here because it broke once:

1. **Stage into a fresh directory.** Don't repack the previous bundle in place; the
   working copy collects debug leftovers. v1.4.0 shipped 64 MB of them, including
   212 developer run logs.
2. **A public bundle carries no game data.** Ship the exe, the 8 module DLLs, the
   SDK runtime + GPU plugin, `futari.ini`, `extract_assets.bat` +
   `extract_iso.py` (so the user can populate `assets/` themselves), `RUN.txt`
   copied from [`dist/RUN.txt`](dist/RUN.txt), `gamecontrollerdb.txt` copied from
   [`dist/gamecontrollerdb.txt`](dist/gamecontrollerdb.txt) (SDL's community
   gamepad-mapping database — the SDL input driver loads it from beside the exe
   for pads SDL doesn't know natively), an empty `dlc/` holding
   [`dist/dlc-README.txt`](dist/dlc-README.txt) as `dlc/README.txt` (the CI
   guard rejects any tracked `dlc/` path, hence the flat name here), and
   [`THIRD-PARTY-NOTICES.txt`](THIRD-PARTY-NOTICES.txt). Everything on that list
   is in this repo or produced by the build, so the rule can be executed from a
   clean clone. The exe carries its icon itself (`src/futari.ico`, embedded on
   Windows; `build_app.sh` turns it into the `.app` icon), so the dump's loose
   `icon*.ico` files are not needed. `assets/` and `dlc/` contents are the game —
   they never go in a bundle that leaves your machine. Grep the staged tree for
   `.xex` before zipping.

   ⚠️ Note what "no game data" does and does not mean: the exe and the 8 module
   DLLs *are* the game's own program code, recompiled. That is unavoidable in a
   binary port, and it is why `RUN.txt` and the README say the bundles are for
   owners only — do not describe a bundle as containing no game content. Rule 2
   is about *data*: no disc image, no `media/`, no audio, no artwork.

2b. **Licence text travels with the binary.** The runtime statically links FFmpeg
   and libmspack (both LGPL-2.1), plus ~20 permissive components, every one of
   which requires its notice to accompany a binary distribution. Regenerate the
   notices whenever the vendored SDK tree changes (`build_app.sh` does this
   itself and tells you when the committed copy was stale):

   ```sh
   bash scripts/gen_third_party_notices.sh > THIRD-PARTY-NOTICES.txt
   ```

   The LGPL also obliges a written offer of source and relinkable objects; that
   offer is in the file's header and points at this repository's vendored SDK
   tree at the release tag. That is one more reason a tag must carry exactly the
   SDK the bundle was built from.
3. **Zip with 7z**, under a single top-level `Mushihimesama-Futari-PC/` folder.
   .NET `Compress-Archive` writes non-spec backslash entries and no wrapper folder.
4. **Diff the new zip's file list against the previous release** before publishing,
   and confirm every difference is intended.
5. **Linux bundles: zip on Linux with `zip -X`**, never from Windows. A Windows
   zipper drops the Unix mode bits, so the launcher arrives non-executable.
6. **No absolute paths in anything that ships.** The v1.4.0 Deck launcher and
   `futari.ini` both hardcoded the directory they were tested from, so the bundle
   only ran on that one machine. The launcher must resolve its own location.
7. **Check what a bundle actually contains, not what you meant to put in it.**
   `strings <binary> | grep rexglue-v` reports the SDK each component was built
   against; v1.4.0's Deck zip mixed a v0.8.3 runtime with a v0.9.0-dev binary.
