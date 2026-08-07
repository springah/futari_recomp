# futari_recomp

Static recompilation of **Mushihimesama Futari** (Xbox 360, 2006/2012) for native
PC execution, built on the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk).

> **No game files here.** You must own a copy and extract the assets yourself.

> ⚠️ **Built with heavy use of AI.** Code, tooling, and docs in this repo were
> written largely with LLM assistance. Hobby project — verify before relying on
> it, expect rough edges, no warranty.

![Mushihimesama Futari title screen](screenshots/title.png)

**Latest release:** v1.4.1 (Windows) · v1.4.0 (macOS, Steam Deck) —
see [CHANGELOG.md](CHANGELOG.md). Per-OS build and packaging details:
[PORTS.md](PORTS.md).

**Status:** title, menus, audio, controller, and saves work, and all 8 game modes
load, run, and mode-switch cleanly. Deeper gameplay can still hit rare recompiler
edge cases — see [Known issues](#known-issues).

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

**`assets/` — the disc (required).** Extract your **Ver 1.5** disc image:

```sh
python scripts/extract_iso.py "Mushihimesama Futari Ver 1.5.iso" --out assets/
```

On Windows you can drag the `.iso` onto
[`extract_assets.bat`](extract_assets.bat) instead. You should end up with
`assets/default.xex` and an `assets/media/` tree containing the 8 module XEXs.
The game reads `assets/` next to the exe; `--game_data_root <path>` points
elsewhere.

**`dlc/` — the marketplace DLC (optional).** Drop the Xbox 360 STFS packages into
a `dlc/` folder next to the exe, filenames unchanged. They install themselves on
first launch (`DLC: InstallContent(...) -> 0x00000000` in the log). No `dlc/`
folder just means no extra content — the game runs fine without it.

📖 **[docs/DUMPING.md](docs/DUMPING.md)** walks through getting both off your own
console, and lists checksums to verify the result.

## Build

Needs CMake 3.25+, Ninja, **LLVM/clang-cl** 20+ (at `C:\Program Files\LLVM`), the
Windows SDK (for `rc.exe`), Python 3.8+, and git + bash.

```sh
# 1. Clone with the SDK submodule (pinned at a tagged release)
git clone -c core.symlinks=true --recurse-submodules <repo-url> futari_recomp
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

# 5. Configure
cmake -S . -B out/build/win-amd64 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" \
  -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" \
  -DCMAKE_RC_COMPILER="C:/Program Files (x86)/Windows Kits/10/bin/<sdk-ver>/x64/rc.exe"

# 6. Build everything — the exe plus all 8 module DLLs
cmake --build out/build/win-amd64

# 7. Run (GUI app; logs to logs/futari_NNN.log next to the exe)
out/build/win-amd64/futari.exe --game_data_root assets
```

<details>
<summary><b>Gotchas worth knowing before you hit them</b></summary>

- **clang-cl, not clang++.** This builds against the MSVC ABI. The CMake presets
  default to `clang++`, which is why the configure line above is explicit — and why
  Windows releases don't come from a preset.
- **`-c core.symlinks=true` on clone matters.** The SDK's libmspack submodule uses
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
- **The SDK pin.** Builds use the `thirdparty/rexglue-sdk` submodule at a tagged
  release. Pass `-DREXSDK_DIR=<path>` to build against a live checkout instead.

</details>

## Layout

```
assets/                 # gitignored — your extracted game files
generated/              # gitignored — codegen output (scripts/regen.sh)
futari_manifest.toml    # entrypoint + the 8 game-mode modules
futari_functions.toml   # function-entry hints for the shell
src/main.cpp            # host app: ReXApp subclass + futari.ini config
scripts/                # extract_iso.py, analyse_xex.py, regen.sh
dist/RUN.txt            # the readme that ships inside release zips
thirdparty/rexglue-sdk/ # submodule — the SDK, pinned at a tagged release
```

## Known issues

- **Shell edge case `0x820816EC`** — a function with a backward branch into itself
  that doesn't split cleanly. Only crashes if that path runs.

Historically, entering a game mode host-crashed via under-sized jump tables. Fixed
in the SDK: index-register copy tracing, manual `[[switch_tables]]` support, and
out-of-range cases now fall back to faithful indirect dispatch rather than
`__builtin_trap()`.

## Credits

- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) — Xbox 360 runtime + recompiler (Xenia-derived)
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) — reference PPC → C++ recompiler
- [Xenia](https://github.com/xenia-project) — the emulator ReXGlue's runtime is rooted in
- [UnleashedRecomp](https://github.com/hedge-dev/UnleashedRecomp) — recomp reference project

Also worth a look: [SDOJ-Recomp](https://github.com/eandis/SDOJ-Recomp) — a recomp
of another CAVE Xbox 360 shooter, DoDonPachi SaiDaiOuJou.

Cave / CAVE Interactive Co., Ltd. own all rights to Mushihimesama Futari.
