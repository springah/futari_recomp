# Mushihimesama Futari — Ports

Per-OS build and release notes. Build prerequisites and the Windows walkthrough
live in the [README](README.md); version history is in [CHANGELOG.md](CHANGELOG.md).

## Platforms

| OS | Build | Status |
|----|-------|--------|
| Windows (x64) | manual clang-cl configure → `out/build/win-amd64` (README) | ✅ v1.4.1 (SDK v0.9.3.1) |
| Steam Deck / Linux (x64) | `linux-amd64-release` preset, built in WSL | ✅ v1.4.0 native (built; on-Deck test pending) |
| macOS (arm64) | `mac-arm64-release` preset on the MBP, bundled by `mac-build/build_app.sh` | ✅ v1.4.0 (MoltenVK) |

**Windows builds are not a preset.** The project compiles with clang-cl while the
presets default to `clang++`, so releases come from a hand-configured
`out/build/win-amd64`. Build with no `--target` so the 8 module DLLs get the same
optimisation flags as the exe.

## SDK

All builds link the `thirdparty/rexglue-sdk` submodule, pinned at a tagged release
(currently **v0.9.3.1**), so every build records an SDK SHA. `-DREXSDK_DIR=<path>`
overrides it with a live checkout.

To bump the pin: tag an SDK release, then
`git -C thirdparty/rexglue-sdk fetch --tags && git -C thirdparty/rexglue-sdk checkout vX.Y.Z`,
and commit the gitlink.

⚠️ Check what a build directory is pinned to before shipping from it —
`grep REXSDK_DIR out/build/<dir>/CMakeCache.txt`. `out/build/win-amd64-release` is
stale and still points at a live sibling checkout rather than the submodule.

## Releases

One SemVer tag per release; per-OS zips (`Mushihimesama-Futari-PC-vX.Y.Z.zip`,
`-Deck-`, `-macOS-`) go to the SHARED download share and the Drive mirror. The repo
stays code + tags: no GitHub Releases, no game content.

Packaging rules — each one is here because it broke once:

1. **Stage into a fresh directory.** Don't repack the previous bundle in place; the
   working copy collects debug leftovers. v1.4.0 shipped 64 MB of them, including
   212 developer run logs.
2. **Futari ships more than exe + DLLs + `assets/`** — also `dlc/` (two STFS
   packages installed on launch), `icon1.ico`, `icon2.ico`, `futari.ini`, and
   `RUN.txt` copied from [`dist/RUN.txt`](dist/RUN.txt).
3. **Zip with 7z**, under a single top-level `Mushihimesama-Futari-PC/` folder.
   .NET `Compress-Archive` writes non-spec backslash entries and no wrapper folder.
4. **Diff the new zip's file list against the previous release** before publishing,
   and confirm every difference is intended.
