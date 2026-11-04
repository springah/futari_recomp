# Renut leg 6: macOS (Apple Silicon, MoltenVK)

2026-11-04, branch `leg/macos` (cut from `launcher` at 3400733). Leg 6 of `docs/NOTES_renut_port_scoping.md`.
Machine: M3 Max, macOS 27.2, Homebrew LLVM (presets), Homebrew MoltenVK, CMake 4.4.

## Result

| what | state |
|---|---|
| `cmake --preset mac-arm64-release` with `FUTARI_RENUT_RENDERER=ON` (dev tree and `-DFUTARI_RENUT_RELEASE_SHADERS=ON`) | builds, links, 0 errors |
| launcher setup screen, loose-folder route, headless (`--futari_setup_source`) | 54 files, 2 DLC, 42 shaders in ~1-4 s, game boots |
| launcher's `vulkan_spirv.bin` | 73,040 bytes, sha256 `757af820…`: **byte-identical to the Windows launcher's package** |
| default renderer (xenos over MoltenVK) | title screen, as before |
| `--futari_renderer=renut` | title screen on plume Vulkan over MoltenVK, 60.00 fps, render thread 0.38 ms, GPU 0.45 ms a frame, ~20k draws / 10 s, 0 shader misses, no errors |
| release-shaders build reading the launcher's package | 42 shaders from `vulkan_spirv.bin`, same picture, 60 fps |
| `.app` from `mac-build/build_app.sh` (SKIP_ASSETS=1, release-shaders tree) | sealed at build; setup + shader step + Renut boot from the bundle (MoltenVK from Contents/lib, DXC from Contents/lib, header from Contents/Resources/renut_tools) |
| setup screen drawn by Renut's overlay drawer (`--futari_setup --futari_renderer=renut`) | draws, with the "Also prepare the Renut renderer" step |

Only the title screen was seen. Nobody has played a stage on Renut on macOS yet.

## DXC decision

No macOS DXC exists anywhere official: Homebrew has no formula (`brew info directx-shader-compiler`: none),
hedge-dev's dxc-bin (vendored by XenosRecomp) ships Windows binaries only, and the one on this Mac
(GPTK's D3DMetal `libdxcompiler.dylib`) is Apple's and not redistributable. Chosen: **build
microsoft/DirectXShaderCompiler v1.8.2407 from source** (the release the Windows tree's dxc.exe/dxcompiler.dll
are): `scripts/build_dxc_mac.sh`, ~10 min on 6 cores, AppleClang, two patches to the clone only (CMake 4 refuses
`cmake_policy(SET CMP0051 OLD)`; the new libc++ refuses LLVM 3.7's `is_nothrow_constructible` specialisations
without `-Wno-invalid-specialization`). Output: a 26 MB `libdxcompiler.dylib` linking only libSystem, libc++
and libz, plus `dxc`, installed git-ignored into `thirdparty/reblue-XenosRecomp/thirdparty/dxc-bin/{bin,lib}/arm64`
(the paths dxc-bin's CMakeLists already names for Apple arm64). The same SPIR-V comes out as on Windows
(the package hash above).

At run time the exe's translator `dlopen`s the dylib, as Windows `LoadLibrary`s dxcompiler.dll: never at the
game's start, only in the `--futari_renut_translate` child. The bundle carries it in `Contents/lib` with its
licence (`Contents/Resources/LICENSE-DirectXShaderCompiler.txt`, University of Illinois/NCSA). Nothing of DXC is
committed.

## What changed (commits on leg/macos)

- `CMakeLists.txt`: OBJC/OBJCXX at the root with the presets' compiler (plume's own `enable_language` left the
  root's `.mm` without a rule: "CMAKE_OBJCXX_COMPILE_OBJECT not set"); UniformTypeIdentifiers for the picker.
- `src/launcher/file_picker_mac.mm`: NSOpenPanel, non-modal (`beginWithCompletionHandler`) on the main thread,
  polled by the dialog. `FilePicker::Impl` moved to `file_picker_impl.h` (it was defined twice, an ODR clash).
- `src/renut_engine/futari_renut_core.h`: the cores reach the exe's `FutariRenutEntries` / `FutariRenutCallWithFree`
  with `dlsym(RTLD_DEFAULT)`, as the PC with GetProcAddress (Linux leaves them undefined; macOS dylibs may not).
- `src/renut_engine/gpu/backend/native_window{.cpp,_apple.mm}`: plume's Apple RenderWindow is
  {NSWindow, CAMetalLayer}; the layer is the one on the SDK window's SDL Metal view.
- `src/renut_engine/gpu/backend/backend.cpp` + one tagged line in `thirdparty/plume/plume_vulkan.cpp`: MoltenVK
  loaded by full path (Contents/lib, Vulkan SDK, Homebrew), volk handed its `vkGetInstanceProcAddr`. volk's own
  bare-name dlopen finds nothing in a bundle or in /opt/homebrew/lib.
- `cmake/futari_renut_gpu.cmake`: the macOS DXC by default, the in-exe translator on Apple
  (`-fexperimental-library` for libc++'s `std::execution`), `renut_tools/` in the build tree,
  `PLUME_APPLE_RETINA_ENABLED OFF` (below).
- Translator: `renut_shader_translate_tool.cpp` and XenosRecomp's `dxc_compiler.cpp` dlopen
  `libdxcompiler.dylib`; `shader_pipeline.cpp` finds the tool files beside the exe or in the bundle.
- `mac-build/build_app.sh`: bundles the dylib, the header and the licence; refuses a SKIP_ASSETS bundle from a tree
  with the game shaders built in (non-release Renut build = game data).

## MoltenVK gaps hit

- **Swapchain rebuilt every frame** (2,700 in 45 s): plume sized it in backing pixels (3600x2072, Retina) while the
  SDK window's SDL Metal layer is 1x (1800x1036); MoltenVK answered every present with VK_SUBOPTIMAL_KHR, which
  plume's Apple path counts as failure. Fixed with `PLUME_APPLE_RETINA_ENABLED OFF` (swapchain at the layer's
  size, 4 creations a run). The output is therefore 1x, upscaled by the compositor, the same as the xenos path
  without `sdl_high_pixel_density`. A Retina swapchain would need the layer's `contentsScale` set to match.
- **No geometry shaders** (Apple GPUs): Renut logs "rectangle lists are drawn as triangle lists and point sprites as
  points". Its own fallback; the title looked right, but sprites/bullets in a stage are where this would show.
  Gate it in play.
- `VK_KHR_present_wait`: not checked; Renut runs without it.

## Not done / open

- **Hands**: no stage was played on Renut on macOS; the picker, Cancel, Settings and F2 were not clicked (headless
  routes only). Renut's overlay input on macOS: `overlay::Start` is Windows-only (it subclasses the HWND), so the log
  says "Overlay input unavailable: no game window", as on Linux. Whether the SDK's input still reaches the launcher /
  F2 dialog when Renut draws them is untested; check with a mouse first.
- **Code seal at run time**: the exe writes `vulkan_spirv.bin`, `logs/`, `dlc/` and `futari.ini` beside itself, so a
  bundle that ran fails `codesign --verify --strict` afterwards. This was already true for logs/ini/assets before leg 6;
  the package just adds one more file. Moving the writable data to `~/Library/Application Support` is the real fix.
- The dev (non-release) build compiles translated game shaders into the exe; only the release-shaders tree may be
  bundled for others (build_app.sh enforces it for SKIP_ASSETS=1).
- Configure needs a Python with `xxhash` + `numpy` for the build-time cache
  (`-DPython3_EXECUTABLE=<venv>/bin/python`), and `out/renut_shader_dump` from `scripts/renut_shader_dump.sh`.

## How to verify

```sh
git submodule update --init --recursive
cmake -S thirdparty/rexglue-sdk -B out/sdkcli -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DREXGLUE_BUILD_TESTS=OFF && ninja -C out/sdkcli rexglue
# assets/ = default.xex + media/Module/*.xex of your own dump
bash scripts/regen.sh
python3 -m venv out/venv && out/venv/bin/pip install xxhash numpy
PATH=$PWD/out/venv/bin:$PATH bash scripts/renut_shader_dump.sh
scripts/build_dxc_mac.sh
cmake --preset mac-arm64-release -DREXGLUE_BUILD_TESTS=OFF -DFUTARI_RENUT_RELEASE_SHADERS=ON \
  -DPython3_EXECUTABLE=$PWD/out/venv/bin/python
ninja -C out/build/mac-arm64-release -j6
SKIP_ASSETS=1 APP=$PWD/out/test/Futari.app bash mac-build/build_app.sh
out/test/Futari.app/Contents/MacOS/futari --game_data_root=<empty>/assets --futari_setup_source=<your loose folder>
out/test/Futari.app/Contents/MacOS/futari --game_data_root=<that>/assets --futari_renderer=renut
grep -iE 'error|miss|warn|fatal|10.0 s:' out/test/Futari.app/Contents/MacOS/logs/futari_*.log
```

Expected: "Shaders ready: 42 translated", "SPIR-V package …: 42 shaders", "MoltenVK from …/Contents/lib", ~60 fps
lines, and only the two known warnings (no geometry shaders; overlay input).
