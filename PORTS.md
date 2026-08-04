# Mushihimesama Futari — Ports

Recompiled PC port of the Xbox 360 XBLA title, built on the **rexglue-sdk** static
recompiler. You must supply your own game dump (`default.xex` + assets) — this repo
ships no game content. (Futari was the first recomp port; its `legacy/` tree holds
the original XenonRecomp-based output, kept for reference only — not part of the
active build.)

## Platforms
| OS | CMake preset | Status |
|----|--------------|--------|
| Windows (x64) | `win-amd64-release` | ✅ v1.3.6 (SDK v0.8.4 F4 settings overlay rework) |
| Steam Deck / Linux (x64) | `linux-amd64-release` | ✅ v1.3.6 native linux-amd64 |
| macOS (arm64) | `mac-arm64-release` | ✅ v1.3.6 on the MBP (MoltenVK; see `mac-build/`) |

## SDK
All builds link the **rexglue-sdk** submodule at `thirdparty/rexglue-sdk`,
**pinned at a tagged SDK release** (currently v0.9.1) — `cmake --preset
win-amd64-release` resolves it with no flags or env vars. The presets no longer
read the `REXSDK_DIR` environment variable (the persistent user-profile value
still serves the sibling ports); to build futari against a live SDK checkout,
pass it explicitly: `cmake --preset win-amd64-release -DREXSDK_DIR=<path>`.
To bump the pin: tag a new SDK release, `git -C thirdparty/rexglue-sdk fetch
--tags && git -C thirdparty/rexglue-sdk checkout vX.Y.Z`, commit the gitlink.

## Build & stage
- **Windows:** `cmake --preset win-amd64-release && cmake --build --preset win-amd64-release`
  (configures + builds Release). Package: `projects\_stage_release.ps1 -ProjDir <repo>`
  → double-clickable layout (exe + DLLs + `assets/`, no flags).
- **Steam Deck / Linux:** regenerate code with `scripts/regen.sh` if needed, then build
  the `linux-amd64-release` preset in WSL (`~/sdk-build/`), deploy via `Desktop/deck_tools`
  (`ssh deck`). Native linux-amd64 (no Proton).
- **macOS:** build the `mac-arm64-release` preset on the MBP (`ssh mbp`, Homebrew LLVM),
  bundle into a `.app` via `mac-build/build_app.sh` (MoltenVK).

## Releases
One SemVer tag `vX.Y.Z`; per-OS zips (`futari-windows.zip` / `-deck.zip` /
`-macos.zip`) published to the SHARED download share. The repo stays code + tags
only — no GitHub Releases, no game content.
