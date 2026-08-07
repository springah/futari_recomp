# Mushihimesama Futari — Ports

Recompiled PC port of the Xbox 360 XBLA title, built on the **rexglue-sdk** static
recompiler. You must supply your own game dump (`default.xex` + assets) — this repo
ships no game content. (Futari was the first recomp port; its `legacy/` tree holds
the original XenonRecomp-based output, kept for reference only — not part of the
active build.)

## Platforms
| OS | Build config | Status |
|----|--------------|--------|
| Windows (x64) | manual clang-cl configure → `out/build/win-amd64` (see README "Build") | ✅ v1.4.1 (SDK v0.9.3.1, rexgpu-xenos plugin) |
| Steam Deck / Linux (x64) | `linux-amd64-release` | ✅ v1.4.0 native linux-amd64 (built; on-Deck test pending) |
| macOS (arm64) | `mac-arm64-release` | ✅ v1.4.0 on the MBP (MoltenVK; see `mac-build/`) |

## SDK
All builds link the **rexglue-sdk** submodule at `thirdparty/rexglue-sdk`,
**pinned at a tagged SDK release** (currently v0.9.3.1) — CMake resolves it with
no flags or env vars. The presets no longer read the `REXSDK_DIR` environment
variable (the persistent user-profile value still serves the sibling ports); to
build futari against a live SDK checkout, pass `-DREXSDK_DIR=<path>` at configure
time. ⚠️ Check what a build directory is actually pinned to before shipping from
it: `grep REXSDK_DIR out/build/<dir>/CMakeCache.txt`. `out/build/win-amd64-release`
is a stale tree that still points at a live sibling checkout, not the submodule.
To bump the pin: tag a new SDK release, `git -C thirdparty/rexglue-sdk fetch
--tags && git -C thirdparty/rexglue-sdk checkout vX.Y.Z`, commit the gitlink.

## Build & stage
- **Windows:** the shipping build is **not** a preset. This project compiles with
  **clang-cl** (MSVC ABI) while the presets default to `clang++`, so releases are
  configured by hand into `out/build/win-amd64` — see README **Build** steps 5–6 for
  the exact command — then `cmake --build out/build/win-amd64` (no `--target`, so the
  8 module DLLs get the optimisation flags too). Package per **Releases** below;
  `projects\_stage_release.ps1` builds the *asset-free* experimental layout and is
  **not** the ship path.
- **Steam Deck / Linux:** regenerate code with `scripts/regen.sh` if needed, then build
  the `linux-amd64-release` preset in WSL (`~/sdk-build/`), deploy via `Desktop/deck_tools`
  (`ssh deck`). Native linux-amd64 (no Proton).
- **macOS:** build the `mac-arm64-release` preset on the MBP (`ssh mbp`, Homebrew LLVM),
  bundle into a `.app` via `mac-build/build_app.sh` (MoltenVK).

## Releases
One SemVer tag `vX.Y.Z`; per-OS zips (`Mushihimesama-Futari-PC-vX.Y.Z.zip` /
`-Deck-` / `-macOS-`) published to the SHARED download share and mirrored to
Drive. The repo stays code + tags only — no GitHub Releases, no game content.

**Packaging rules** (each one exists because it was broken once):
- Stage into a **fresh** directory and copy in only what ships. Do *not* repack the
  previous bundle in place: the working copy accumulates debug leftovers, and the
  v1.4.0 zip went out carrying 64 MB of `*.orig` / `*.pre_*` backups, the
  instrumented `futari_ref.exe`, `TracyClient.dll`, 212 developer run logs, and a
  machine-local `.lnk`.
- Futari ships **more than exe + DLLs + `assets/`**: it also needs `dlc/` (two STFS
  packages the runtime installs on launch), `icon1.ico`, `icon2.ico`, `futari.ini`,
  and `RUN.txt` (copy it from [`dist/RUN.txt`](dist/RUN.txt), which is the reviewed
  source — it lived only inside the zip for a long time, which is how a mojibake
  `???` survived several releases).
- Zip with **7z** under a single top-level `Mushihimesama-Futari-PC/` wrapper folder.
  .NET `Compress-Archive` writes non-spec backslash entries and no wrapper — that is
  what produced the v1.4.0 zip.
- Before publishing, **diff the new zip's file list against the previous release**
  and confirm the only differences are intended.
