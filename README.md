# futari_recomp

Static recompilation of **Mushihimesama Futari** (Xbox 360, 2006/2012) for native
PC execution, built on the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk).

> **No game data here.** No game executable, no `media/`, no audio, no artwork
> off the disc — you supply all of it yourself, from a copy you own; see
> [Game data](#game-data) and [docs/DUMPING.md](docs/DUMPING.md). The two
> exceptions are named in [NOTICE.md](NOTICE.md): a title screenshot and the
> application icon, both game-derived and both kept for identification only.

> ⚠️ **Built with heavy use of AI.** Code, tooling, and docs in this repo were
> written largely with LLM assistance. Hobby project — verify before relying on
> it, expect rough edges, no warranty.

![Mushihimesama Futari title screen](screenshots/title.png)

**Latest release:** v1.5.3 (Windows, macOS, Steam Deck), with the bundles
attached to the [GitHub release](https://github.com/springah/futari_recomp/releases/tag/v1.5.3).
See [CHANGELOG.md](CHANGELOG.md). Per-OS build and packaging
details: [PORTS.md](PORTS.md).

**Status:** title, menus, audio, controller, and saves work, and all 8 game modes
load, run, and mode-switch cleanly. Nothing is known to be broken right now; the
recompiler edge cases found so far, all fixed, are under
[Known issues](#known-issues). Deeper gameplay may still turn up new ones.

## Just want to play?

From v1.5.3 on, prebuilt bundles for Windows, macOS and Steam Deck are attached
to the [GitHub Releases](https://github.com/springah/futari_recomp/releases)
(the v1.5.2 bundles were only ever shared privately). The bundles contain the game's own program code,
recompiled, and are **for people who own the game only** — see [Legal](#legal).
They contain no game data: unzip one and follow `RUN.txt` inside to add your own
dump (on Windows, drag your disc ISO or console install folder onto
`extract_assets.bat`, then double-click `futari.exe`).

## How it works

The Xbox 360 PowerPC code is translated ahead-of-time to C++ and linked against
the ReXGlue runtime (a Xenia-derived layer providing the kernel, a D3D12/Vulkan
GPU, XMA audio, input, and a virtual filesystem). `default.xex` is the shell and
menu; each of the 8 game modes is a separate module XEX, recompiled into its own
DLL and loaded on demand.

Almost none of that C++ is in this repo — it's generated into `generated/` by
`scripts/regen.sh` from your own game dump, and gitignored. What's tracked here is
the host app (`src/main.cpp`), the codegen inputs (`*.toml`), and the build.

## Game data

You supply the game yourself, from a copy you own. Two pieces:

**`assets/` — the disc (required).** The extractor takes either a **Ver 1.5**
disc ISO or the install folder copied straight off your console's storage
(hash-verified, no conversion tools needed — see DUMPING.md):

```sh
python scripts/extract_iso.py "Mushihimesama Futari Ver 1.5.iso" --out assets/
# or, from a console disc install (also installs your DLC into dlc/):
python scripts/extract_iso.py "path/to/435A07D7" --out assets/ --dlc-out dlc/
```

On Windows you can drag the `.iso` — or that console folder — onto
[`extract_assets.bat`](extract_assets.bat) instead. You should end up with
`assets/default.xex` and an `assets/media/` tree containing the 8 module XEXs.
The game reads `assets/` next to the exe; `--game_data_root <path>` points
elsewhere.

**`dlc/` — the marketplace DLC (optional).** The extractor installs these
automatically when it finds them next to a console install; or drop the Xbox 360
STFS packages into a `dlc/` folder next to the exe yourself, filenames unchanged.
They install themselves on first launch
(`DLC: InstallContent(...) -> 0x00000000` in the log). No `dlc/` folder just
means no extra content — the game runs fine without it.

📖 **[docs/DUMPING.md](docs/DUMPING.md)** walks through getting both off your own
console, and lists checksums to verify the result.

## Build

Needs CMake 3.25+, Ninja, **LLVM/clang-cl** 20+ (at `C:\Program Files\LLVM`),
**Visual Studio 2022 Build Tools** with the Desktop C++ workload (clang-cl targets
the MSVC ABI, so it needs those headers and libs — plus `rc.exe` from the bundled
Windows SDK), Python 3.8+, and git + bash.

**Quick start (Windows):** [`build.ps1`](build.ps1) does all of the below in one
go. It installs whatever is missing from that list via winget, enters the Visual
Studio developer environment, builds the SDK CLI, extracts your game data, runs
codegen, configures and builds:

```powershell
git -c core.symlinks=true -c core.longpaths=true clone --recurse-submodules https://github.com/springah/futari_recomp
cd futari_recomp
.\build.ps1 -GameSource "D:\Mushihimesama Futari Ver 1.5.iso" -Run
```

`build.bat` is the same thing for a double-click. Git symlinks need **Developer
Mode** (Settings → System → For developers) or an elevated shell; the script
says so if it hits that.

The SDK at `thirdparty/rexglue-sdk` is **vendored** — this port owns and
maintains its own copy, taken from a fork of the
[upstream SDK](https://github.com/rexglue/rexglue-sdk) at v0.9.3.1 and carrying
the clang-cl and macOS fixes this port needs; see
[thirdparty/rexglue-sdk/VENDORED.md](thirdparty/rexglue-sdk/VENDORED.md) and
[PORTS.md](PORTS.md#sdk). Only the SDK's third-party dependencies are git
submodules, all pointing at their public upstreams.

The manual steps `build.ps1` automates:

```sh
# 1. Clone with the SDK's third-party submodules (~1 GB of dependency history;
#    do NOT add --shallow-submodules — several pins are not branch tips, and a
#    shallow clone silently leaves those submodules on the wrong commit).
#    The -c options go BEFORE `clone` so they reach the submodule clones too —
#    `git clone -c ...` only configures the parent repo.
git -c core.symlinks=true -c core.longpaths=true clone --recurse-submodules https://github.com/springah/futari_recomp
cd futari_recomp

# 2. Build the ReXGlue CLI
cmake -S thirdparty/rexglue-sdk -B thirdparty/rexglue-sdk/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" \
  -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe"
cmake --build thirdparty/rexglue-sdk/build --target rexglue

# 3. Provide the game data — see "Game data" above (assets/, and dlc/ if you have it)
python scripts/extract_iso.py "Mushihimesama Futari Ver 1.5.iso" --out assets/

# 4. Generate the recompiled C++ into generated/
bash scripts/regen.sh

# 5. Configure. Run from a "Developer PowerShell for VS 2022" so rc.exe is on
#    PATH; otherwise add -DCMAKE_RC_COMPILER="C:/Program Files (x86)/Windows Kits/10/bin/<sdk-ver>/x64/rc.exe".
#    The win-amd64-* presets use clang-cl from C:/Program Files/LLVM; pass
#    -DCMAKE_C_COMPILER/-DCMAKE_CXX_COMPILER if yours lives elsewhere.
cmake --preset win-amd64-release

# 6. Build everything — the exe plus all 8 module DLLs
cmake --build --preset win-amd64-release

# 7. Run (GUI app; logs to logs/futari_NNN.log next to the exe)
out/build/win-amd64-release/futari.exe --game_data_root assets
```

<details>
<summary><b>Gotchas worth knowing before you hit them</b></summary>

- **clang-cl, not clang++.** This builds against the MSVC ABI. The `win-*`
  presets name clang-cl for that reason; the SDK's own presets still default to
  `clang++`, which is why step 2 spells the compiler out.
- **`git -c core.symlinks=true` (before `clone`) matters.** The SDK's libmspack submodule uses
  git symlinks. Without it (plus Developer Mode), those files check out as text
  stubs and the SDK build fails with `lzxd.c(1,1): error: expected identifier`. Fix
  an affected checkout with `git -C <submodule> config core.symlinks true`, then
  re-checkout the files.
- **Don't pass `-DCMAKE_CXX_FLAGS=...`.** It clobbers CMake's default `/EHsc` and
  breaks exception handling in dependencies. The SSE baseline and `char8_t` flags
  are set inside the SDK's own CMake.
- **Build with no `--target`.** Naming only `futari` skips the 8 module DLLs, which
  then keep whatever flags they were last built with.
- **Codegen runs per-module in isolation.** All 8 modes load at `0x88000000`, and
  the SDK analyzer corrupts cross-module analysis if they share one manifest.
  `regen.sh` already handles this; don't collapse it into a single codegen pass.
- **The SDK is vendored, not pinned.** Builds use the `thirdparty/rexglue-sdk`
  tree in this repo. Pass `-DREXSDK_DIR=<path>` to build against a separate
  checkout instead.
- **`generated/` must exist before you configure.** `CMakeLists.txt` includes
  `generated/rexglue.cmake`, which step 4 produces. Configuring a fresh clone
  without running `scripts/regen.sh` first fails immediately, by design.

</details>

### macOS and Linux

The same seven steps, with a different compiler and preset. Needs CMake 3.25+,
Ninja, Python 3 and a recent clang: on macOS that is **Homebrew LLVM**
(`brew install llvm molten-vk`; the SDK rejects Apple's clang), on Linux the
distribution's clang 20+ plus the Vulkan headers and loader.

```sh
# 2. Build the ReXGlue CLI with the SDK's own preset for your platform
#    (linux-amd64 or mac-arm64). The binary lands in thirdparty/rexglue-sdk/out/<platform>/,
#    which is where regen.sh looks for it.
cmake --preset mac-arm64 -S thirdparty/rexglue-sdk      # or: linux-amd64
cmake --build --preset mac-arm64 --target rexglue       # or: linux-amd64

# 3.–4. as above
python3 scripts/extract_iso.py "Mushihimesama Futari Ver 1.5.iso" --out assets/
bash scripts/regen.sh

# 5.–7. configure, build, run
cmake --preset mac-arm64-release                        # or: linux-amd64-release
cmake --build --preset mac-arm64-release
out/build/mac-arm64-release/futari --game_data_root assets
```

On macOS, `mac-build/build_app.sh` turns that build directory into a
self-contained `Futari.app` (bundled MoltenVK, ad-hoc signed); its header lists
the paths it takes. [PORTS.md](PORTS.md) has the per-platform notes.

## Layout

```
assets/                   # gitignored — your extracted game files
dlc/                      # gitignored — your DLC packages
generated/                # gitignored — codegen output (scripts/regen.sh)
futari_functions.toml     # function-entry hints for the shell (default.xex)
<module>_functions.toml   # the same for each of the 8 game-mode modules
src/                      # host app: main.cpp (ReXApp subclass, futari.ini, title exit), ini_merge.h, icon
scripts/                  # extract_iso.py, regen.sh, build_module_hints.sh, analyse_xex.py, notices generator
build.ps1, build.bat      # one-shot Windows build
extract_assets.bat        # drag-and-drop wrapper around extract_iso.py (also ships in the bundles)
mac-build/build_app.sh    # macOS .app packaging
dist/                     # files that ship inside release zips: RUN.txt, gamecontrollerdb.txt, dlc-README.txt
docs/                     # DUMPING.md (getting your own game data), upstream/ (SDK notes)
PORTS.md, CHANGELOG.md    # per-OS build/release notes; version history
thirdparty/rexglue-sdk/   # the SDK, vendored (VENDORED.md); its deps are submodules
```

## Known issues

None currently known. Two earlier ones, for the record:

- **The shell's one inexpressible branch (fixed).** Codegen used to split the
  block at `0x82081778` into its own function, though it is really an interior
  continuation of `sub_82081658` — register restores that rejoin the parent's
  loop at `0x820816EC`, a branch that cannot be expressed from a separate
  function, so it compiled to the shell's only `REX_FATAL`. The split turned
  out to be a pointer-scan false positive: the only "pointer" to that address
  in the whole image is a `lwz r16,0x1778(r8)` instruction, whose encoding
  happens to equal `0x82081778`. The address is now declared a chunk of its
  parent in `futari_functions.toml` (the SDK's function config `parent` field,
  whose semantics were completed for this), no standalone function or dispatch
  entry is emitted, and the fatal is gone.
- Historically, entering a game mode host-crashed via under-sized jump tables.
  Fixed in the SDK: index-register copy tracing, manual `[[switch_tables]]`
  support, and out-of-range cases now fall back to faithful indirect dispatch
  rather than `__builtin_trap()`.

## Credits

- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) — Xbox 360 runtime + recompiler (Xenia-derived)
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) — reference PPC → C++ recompiler
- [Xenia](https://github.com/xenia-project) — the emulator ReXGlue's runtime is rooted in
- [UnleashedRecomp](https://github.com/hedge-dev/UnleashedRecomp) — recomp reference project

Also worth a look: [SDOJ-Recomp](https://github.com/eandis/SDOJ-Recomp) — a recomp
of another CAVE Xbox 360 shooter, DoDonPachi SaiDaiOuJou.

## Legal

Mushihimesama Futari, and all of its code, data, audio, and artwork, are the
property of Cave Interactive Co., Ltd. This project is not affiliated with,
endorsed by, or connected to Cave in any way.

Nothing here is a substitute for owning the game:

- **No game data is distributed.** No disc image, no game executable, no
  `media/`, no audio, no artwork off the disc — not here and not in the release
  bundles. The port is inert without a dump you provide. The only game-derived
  files in this repository are the title screenshot and the application icon,
  both for identification; see [NOTICE.md](NOTICE.md).
- **The recompiled C++ is produced on your machine, from your own disc.** It is
  a translation of Cave's executable code, so it belongs to you no more than the
  disc does. Don't redistribute it. The release bundles necessarily contain it,
  compiled, and are inert without a dump you provide. They are published on the
  same footing as other recompilation projects: for people who already own the
  game. If you don't own it, don't download them.
- **Do not ask for game files.** Requests for dumps, links, or "where do I get
  the ISO" will be closed. [docs/DUMPING.md](docs/DUMPING.md) covers the only
  supported route: your own disc, read with your own console.

## Licence

The contents of this repository — the host app, the tooling, the codegen inputs,
the build files, and the docs — are under the
[BSD 3-Clause licence](LICENSE), matching the SDK's own licence and the Xenia
lineage behind it. That licence covers **this repository only**; it grants no
rights whatsoever to the game, or to the recompiled code generated from it.
[NOTICE.md](NOTICE.md) spells out exactly what is and is not covered.

Contributions welcome — see [CONTRIBUTING.md](CONTRIBUTING.md).
