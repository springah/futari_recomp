# Futari → macOS — workspace status

Prepped 2026-06-14. Goal: get `futari_recomp` (Mushihimesama Futari Ver. 1.5,
Xbox 360 static recomp on the ReXGlue SDK) building & running natively on macOS.

---

## ✅ ALPHA RESULT (2026-06-14) — SDK builds & runs natively on macOS arm64

The ReXGlue SDK + recompiler toolchain now **compiles, links, and runs on Apple
Silicon** from the PR #356 merge.

- Branch: `futari-mac-merge` in the SDK submodule (PR #356 merged into the fork's
  `futari-clang-cl-fixes`; 2 conflicts resolved — see below).
- Toolchain: **Homebrew LLVM clang 22.1.7** (the SDK gates on real Clang ≥18;
  Apple clang is rejected by `CMakeLists.txt:83` on both ID and version). Also
  installed: cmake 4.3.3, ninja 1.13.2, molten-vk 1.4.1.
- Configure: `cmake --preset mac-arm64` ✅ (Vulkan ON, D3D12 OFF, C++23, OBJC/OBJCXX).
- Build: `cmake --build out/build/mac-arm64 --config Release --target rexglue` ✅
  — all 852 steps; produced `out/mac-arm64/Release/rexglue` (arm64 Mach-O) and
  `librexruntime.dylib`. `./rexglue --version` → `0.8.1.92-dev.ga011d43`.
- **Fix applied** (committed 72eab46): PR #356's `threading_mac.cpp` omitted
  `rex::thread::EnableHighResolutionTiming()` (it replaces `threading_posix.cpp`
  on Apple, which defined the no-op). Added the Darwin no-op → runtime links.
  *This is a clean upstream PR candidate for #356.*

### ✅✅ FULL ALPHA — futari builds AND runs natively on macOS arm64 (2026-06-14)

Found the extracted Xbox 360 assets in the sibling folder
`~/Desktop/projects/Mushihimesama-Futari-PC-v1.2.0/assets` (the existing Windows
v1.2.0 build: `default.xex` + all 8 module XEXs + media/dlc). Symlinked them in.
(The `~/Downloads/futari15.zip` is the unrelated **arcade** CV1000 ROM set.)

End-to-end, all on macOS / Apple Silicon:
1. **Codegen** — `REXGLUE=<mac rexglue> bash scripts/regen.sh` ✅ generated all 9
   module dirs + default. (Only the known `0x820816EC` shell edge-case warnings.)
2. **Configure** — `cmake --preset mac-arm64-release -DREXSDK_DIR=thirdparty/rexglue-sdk` ✅
3. **Build** — host `futari` + all 8 `libfutari_*.dylib` (970 steps, 0 errors) ✅
4. **Run** — `./futari --game_data_root assets` ✅ The native build:
   - created a **1280×720 MoltenVK swapchain in a CAMetalLayer** on the Retina display
   - initialized SDL 3.5.0 / audio / input / GPU; "Runtime initialized successfully"
   - registered all 8 recompiled modules, loaded `default.xex`
   - init shader storage for title **435A07D7**, **created render pipelines from the
     game's own VS/PS shaders** (Xenos → SPIR-V → MoltenVK → Metal)
   - hit the expected MoltenVK `geometryShader` fallback (PR #356's path)

Two fixes were needed beyond the PR #356 merge:
- SDK: `EnableHighResolutionTiming()` Darwin no-op (commit 72eab46 in the SDK submodule).
- futari: `regen.sh` REXGLUE path made env-overridable + mac-arm64 presets
  (commit 21ae532 on branch `mac-arm64-port` in futari_recomp).

### To run it yourself
```sh
cd ~/Desktop/projects/futari-mac/futari_recomp/out/build/mac-arm64-release
VK_ICD_FILENAMES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json \
  ./futari --game_data_root assets
```
(librexruntime.dylib + libTracyClient.dylib were copied next to the exe;
`assets` is symlinked there. Needs molten-vk installed via brew.)

### 📦 Double-clickable app bundle — `~/Desktop/projects/Futari.app` (1.2GB)
Built by `futari-mac/build_app.sh`. Self-contained and ad-hoc signed
(`com.springah.futari`). Launches via Finder double-click with **no env vars** —
verified: the log shows *"Loaded Vulkan runtime from
…/Futari.app/Contents/lib/libMoltenVK.dylib"* (the **bundled** MoltenVK, not
Homebrew's). On launch it reaches the **title screen + the game's own save-data
dialog** (「セーブデータが見つかりませんでした」) — live game logic, not a static frame.
Layout: exe + all 10 dylibs + `assets/` + `dlc/` in `Contents/MacOS/`; MoltenVK in
`Contents/lib/`; ICD in `Contents/Resources/vulkan/icd.d/` (relative `library_path`
resolves into `Contents/lib`); icon from `icon1.ico`. Rebuild anytime with
`bash futari-mac/build_app.sh`.

### 🐞 First crash fixed — module dlopen (2026-06-14)
Leaving the title into the first mode, the app aborted: `Failed to load shared
library for module 'reco_normal.xex'` → `InvalidFunctionTrap` at a garbage guest
address. Cause: recompiled modules are registered by bare name (`futari_reco_normal`)
and loaded via `DynamicLibrary::Load`; POSIX `dlopen()` doesn't add a `lib`
prefix/`.dylib` suffix and on macOS doesn't search the exe dir, so it couldn't find
`libfutari_reco_normal.dylib` sitting next to the host. Fix (SDK commit ea57c2c):
fallback in `dynlib_posix.cpp` resolves `<exe_dir>/lib<name>.dylib` (`.so` on Linux).
**After the fix it loads the module and reaches the in-game TOP MENU** (Ver1.5 /
Arrange / Black Label / Ver1.01, full character art). Three SDK mac fixes total now:
EnableHighResolutionTiming (72eab46), dynlib module load (ea57c2c), + the merge.
Process gotcha: the rebuilt `librexruntime.dylib` lands in
`thirdparty/rexglue-sdk/out/mac-arm64/`, not `out/build/mac-arm64-release/` — copy
from there when re-staging the bundle, then re-`codesign --force --deep --sign -`.

### Known / not-yet-verified
- Visual confirmation via `screencapture` needs Screen Recording permission (it
  silently no-op'd); the window did open and render per the logs.
- How far into the actual title/menu/gameplay it gets, controller input, audio
  output, and the per-mode gameplay paths are all unverified — this is a boot-and-
  render alpha, not a playthrough.
- `default.xexp` patch-file "not found" warning is benign (optional XEX patch).

---

## What's here

```
futari-mac/
├── STATUS.md                     # this file
├── do-mac-merge.sh               # runs the SDK macOS-support merge (helper only)
└── futari_recomp/                # the game port repo (springah/futari_recomp)
    └── thirdparty/
        ├── XenonRecomp/          # reference recompiler submodule (fetched)
        └── rexglue-sdk/          # the SDK — set up for the merge:
            ├── remote origin   = springah/rexglue-sdk (your fork)
            ├── remote upstream = rexglue/rexglue-sdk
            ├── branch futari-clang-cl-fixes  (current fork tip, c3d47a0)
            ├── branch futari-mac-merge       (clean working branch, off the above)
            └── ref pr356        = upstream PR #356 head (macOS support, 5629902)
```

SDK build-dependency submodules (simde / sdl3 / vulkan-headers / VMA) were
fetched shallow during prep. The SDK's other thirdparty submodules are NOT yet
initialised — run `git submodule update --init` inside the SDK before a build.

## The plan (decision pending)

Upstream **[PR #356 "[Feature] macOS Support"](https://github.com/rexglue/rexglue-sdk/pull/356)**
(by Graine25) adds first-class macOS — arm64 + amd64 — to the SDK: Darwin core
backends (Mach VM memory, SIGBUS exception handler, mach thread suspend/resume),
a Cocoa/SDL-Metal windowing layer, and a MoltenVK-backed Vulkan path. It's OPEN,
mergeable, CI green, but not yet reviewed.

**Recommended sequencing:** wait for #356 to merge upstream, then rebase the fork
onto mainline — rather than carrying a fork-of-a-PR. If you want to start now,
use `do-mac-merge.sh` against the `pr356` ref.

## Conflict analysis (fork's futari-clang-cl-fixes vs PR #356)

Test-merge done during prep. 10 files overlap; git auto-merges 8. **2 real conflicts:**

1. **`include/rex/platform/dynlib.h`** — trivial, "keep both". PR356 widened
   `Load(path, SymbolResolution mode = kLazy)`; fork added `LoadIfPresent()`.
   Independent — keep both lines. The `.cpp` sides auto-merge.

2. **`src/ui/rex_app.cpp`** — small manual fold (~15 min). PR356 *extracted* the
   inline overlay/keybind block into a new `ReXApp::SetupOverlays()` method. The
   fork *edited* that block — added an F11 `bind_fullscreen`, a startup-hints
   toast referencing it, and a matching `UnregisterBind("bind_fullscreen")`.
   Resolution: take PR356's `SetupOverlays()` extraction, then move the fork's
   three fullscreen additions into the new method body + its teardown.

**Also eyeball (auto-merged but semantically overlapping):**
- `src/ui/renderdoc_api.cpp` — fork's RenderDoc injector-detection vs PR356's
  Darwin dlopen branch (same code path).
- `src/ui/vulkan/vulkan_device.cpp` — PR356 disables geometryShader/
  fillModeNonSolid for MoltenVK; fork also edited this file.

## Build prereqs for macOS (from PR #356)

- CMake 3.25+, Ninja, a recent LLVM/clang (Apple clang or Homebrew LLVM)
- **MoltenVK** (`libMoltenVK.dylib`) — loaded via dlopen at runtime, NOT vendored;
  install via the Vulkan SDK for macOS or Homebrew (`molten-vk`).
- Presets added by #356: `mac-arm64-release`, `mac-arm64-relwithdebinfo`,
  `mac-amd64-release`.

## Suggested first smoke test (before touching game assets)

Configure + build just the SDK `rexglue` CLI on `mac-arm64`. That exercises
simde, the POSIX/Darwin core layer, and the toolchain — fastest signal that the
native build is sound before wiring up codegen and graphics.

## Notes

- Static AOT recomp ⇒ no runtime guest JIT ⇒ avoids MAP_JIT/W^X guest-code pain
  (PR356 still uses MAP_JIT for the SDK's own JIT-writable mappings under
  Hardened Runtime).
- Game assets (`default.xex`, module XEXs) are NOT in the repo and never committed
  — extract from your own ISO with `scripts/extract_iso.py`.
- Apple Silicon watch-items: 16 KB pages vs guest 4 KB, MoltenVK feature gaps.
