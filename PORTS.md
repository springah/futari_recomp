# Mushihimesama Futari — Ports

Recompiled PC port of the Xbox 360 XBLA title, built on the **rexglue-sdk** static
recompiler. You must supply your own game dump (`default.xex` + assets) — this repo
ships no game content. (Futari was the first recomp port; its `legacy/` tree holds
the original XenonRecomp-based output, kept for reference only — not part of the
active build.)

## Platforms
| OS | CMake preset | Status |
|----|--------------|--------|
| Windows (x64) | `win-amd64-release` | ✅ v1.3.1 (thunk-leak fix) |
| Steam Deck / Linux (x64) | `linux-amd64-release` | ✅ native linux-amd64 |
| macOS (arm64) | `mac-arm64-release` | ✅ v1.3.1 on the MBP (MoltenVK; see `mac-build/`) |

## SDK
All builds link the shared **rexglue-sdk** via the **`REXSDK_DIR`** environment
variable — an external dependency, not a vendored sibling or submodule. Set it
before configuring, e.g. `REXSDK_DIR=C:\Users\shant\Desktop\projects\rexglue-sdk`
(set persistently in the user profile; the `_resync_build.ps1` helper also sets it).
The preset feeds it through as `"REXSDK_DIR": "$env{REXSDK_DIR}"`, so
`cmake --preset win-amd64-release` resolves the SDK with no manual `-D` flag.

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
