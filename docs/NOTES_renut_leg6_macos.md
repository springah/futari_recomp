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
- **No geometry shaders** (Apple GPUs): rectangle lists were drawn as one triangle each, so every in-game screen was a
  staircase of half-quads. **Fixed on `leg/rectlist` by tessellation** (section below). Point sprites are still single
  pixels.
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

## leg/rectlist (2026-11-04): rectangle lists by tessellation on macOS

Branch `leg/rectlist`, cut from `launcher` at a73058d.

### The defect

Xenos rectangle lists (three corners per rectangle, the GPU makes the fourth) are expanded by
`rectangle_list_gs.hlsl`. Apple GPUs under MoltenVK have no geometry shaders, so `BuildRectangleListShader` left it null
and draw.cpp drew the three corners as a plain triangle: one triangle per sprite, bullet, tile and full-screen pass.
The title happened to survive; every stage screen was diagonal half-tiles.

### Design (approach 1 of 3: tessellation)

MoltenVK reports `tessellationShader` on Apple Silicon. Without geometry shaders but with tessellation:

- **plume** (`thirdparty/plume`, commit 7f9dc71): `RenderGraphicsPipelineDesc::hullShader/domainShader` and
  `RenderDeviceCapabilities::tessellationShader`. The Vulkan back end adds the TESSELLATION_CONTROL/EVALUATION stages,
  `VkPipelineTessellationStateCreateInfo` with patchControlPoints = the list primitive's vertex count (triangle list: 3),
  topology PATCH_LIST, no primitive restart. Device features were already enabled wholesale (`pEnabledFeatures` =
  everything the device reports), so tessellation is on. D3D12 / Metal / deko3d ignore the new fields; nothing sets them
  where geometry shaders exist, so the Windows path is untouched (no enum value was added either).
- **Host shaders** (2bcb607): `rectangle_list.hlsli` (the GS's Vertex struct, right-angle choice and `Opposite`, verbatim),
  `rectangle_list_hs.hlsl` (hs_6_0: hands the 3 corners on rotated so control point 0 is the right-angle corner c,
  then a, b in the given cyclic order, exactly where the GS starts its strip; quad domain, integer partitioning, all tess
  factors 1, `triangle_cw`), `rectangle_list_ds.hlsl` (ds_6_0: at (0,0)/(1,0)/(0,1) returns c/a/b untouched, at (1,1)
  `Opposite(a, b, c)` = a + b - c for every output: the GS's arithmetic, bit for bit, no lerp rounding). Compiled by
  `renut_host_shader()` to SPIR-V (and DXIL on Windows, unused there; both compile with DXC 1.8.2407). DXC gives the
  HS->DS interface alphabetical locations on both sides and the VS->HS / DS->PS interfaces declaration-order locations,
  the same as the GS, so the translated guest VS/PS link unchanged. `rectangle_list_gs.hlsl` is unchanged.
- **Renut** (914cf61): video.cpp builds `rectangle_list_hs/ds` when `!geometryShader && tessellationShader`;
  pipeline.cpp uses them for `key.rectangle_list` when there is no GS; draw.cpp sets `key.rectangle_list` for either
  form. The startup line is now
  `[info] No geometry shader support; rectangle lists are drawn by tessellation (rectangle_list_hs/ds), point sprites as points`
  (a WARN remains only for a device with neither).
- **Winding**: `triangle_cw` in Vulkan's upper-left domain makes (0,0)->(1,0)->(0,1) = c->a->b, so both triangles keep
  the given vertices' winding, as the GS strip does. Checked on the GPU with a throwaway build (not committed) that forced
  culling on rectangle lists: cull-front leaves the full picture with tessellation and the same front half-triangles with
  the old fallback (`out/shots/cf78.png`), cull-back blanks both (`out/shots/cull78.png`). Same facing in both forms.

Approaches 2 (vertex pulling in XenosRecomp) and 3 (CPU expansion) were not needed.

### Dev aid: futari_input_script (1a8b23c)

`--futari_input_script="ms:button[:hold_ms],..."` (init-only, src/dev/input_script.cpp): an SDL3 virtual gamepad
(`SDL_AttachVirtualJoystick`, gamepad type, all buttons/axes) attached once the SDK's SDL driver has inited the gamepad
subsystem; the driver opens it through its normal `SDL_EVENT_GAMEPAD_ADDED` hotplug path ("SDL OnControllerDeviceAdded:
\"futari_input_script\" ... Added at index 0"). Each press is logged. Buttons: a b x y start back up down left right lb rb;
held 100 ms by default. The exe imports SDL from librexruntime.dylib, so it is the same SDL instance as the driver's.
Beware zsh: `"$t:a"` is a path modifier, write `${t}:a`. Never pass it with an empty value (`--futari_input_script=`): the SDK's command line then takes the next argument as
its value (seen: it swallowed `--user_data_root=...`, so the run fell back to the default save folder); leave the flag out.

### Verification

Run from `out/run` (staged build exe + dylibs, `librexgpu-xenos.dylib` copied as `.so`, the setup run's
`vulkan_spirv.bin`), `--game_data_root=out/run/assets --user_data_root=<fresh empty folder under out/fresh/>`, one game
window at a time under `/tmp/futari_drive.lock`. **Always pass a fresh `--user_data_root`**: the default
(`~/.local/share/futari`) holds the user's own saves and options.

Button script that reaches Original mode stage 1 (Ver1.5 -> Arcade -> Score Attack -> LIVE notice OK -> 1P SIDE START ->
Mode Select -> Character Select -> Shot Type -> stage 1) from a fresh save, both renderers, ~70 s after start
(timings drift by a few seconds between runs, hence the repeated pairs):

```
12000:start,14500:a,17000:start,19500:a,22000:start,24500:a,27000:start,29500:a,32000:start,34500:a,
37000:start,39500:a,42000:start,44500:a,47000:start,49500:a,55000:x,57000:y,59000:rb,61000:a,... (x,y,rb,a every 2 s to 89000)
```

(Mode/character/shot-type selects did not take A in these runs; the x/y/rb/a cycle gets through them. The fresh save
showed no extra first-run prompt on this route. The run is killed before the game-over replay-save prompt.)

| run | renderer | result |
|---|---|---|
| `out/shots/f1_*` (grid `f1_grid.png`) | default | menus, stage 1 at 70-94 s |
| `out/shots/f2_*` (grid `f2_grid.png`) | renut | same sequence; stage, ship, enemies, enemy bullets, shots, HUD, frame all whole quads |
| `out/shots/fcmp82.png` | renut (left) vs default (right), 82 s | same picture |
| `out/run/logs/futari_024.log` | renut | 0 errors, 0 MoltenVK warnings, 0 shader misses; 58-60 fps in stage 1, GPU 3.5-14 ms a frame |

The `futari_024.log` "Renut shaders are STALE: ... no vulkan_spirv.stamp" warning is the out/run test dir's (its package
was copied without the stamp before this leg); all 42 shaders load.

### Point sprites by tessellation (c6eef37, 0275a48)

Same pattern for `point_sprite_gs.hlsl`: `point_sprite_hs.hlsl` (patch of 1 control point, quad domain at level 1,
tess factors 0 for a killed vertex = patch discarded, as the GS emits nothing) and `point_sprite_ds.hlsl` (the GS's size
rule and corners: domain (u, v) -> corner (2u-1, 1-2v), point coordinate (u, v), same `triangle_cw` winding argument as
above). The DS reads SharedConstants (point state at 1312, viewport at 1328) the GS's way, through the draw's push
constants (buffer device address); plume gained `RenderShaderStageFlag::TESS_CONTROL/TESS_EVALUATION` (not `DOMAIN`:
macOS math.h defines it) mapped to the Vulkan stage bits in push-constant ranges, and Renut's draw layout adds
TESS_EVALUATION to its push range **only** when the device has no GS but has tessellation, so the Windows layout is
byte-identical. Startup line now:
`No geometry shader support; rectangle lists are drawn by tessellation (rectangle_list_hs/ds), point sprites by tessellation (point_sprite_hs/ds)`.
The first point-list draw of a run is logged (`First point-sprite draw: <form>`).

**Futari does not seem to draw point sprites.** Static: the 42 translated shaders (XenosRecomp's HLSL dump of
`out/renut_shader_dump`) have no vertex shader writing a point size (`oPts` is only the zero-initialised default in all
18) and no pixel shader reading the point coordinate. Runtime: a fresh-save Renut run through the Now Loading plate
(ring particles visible, `out/shots/p1_*`) into stage 1 logged no point-list draw at all; the ring particles are ordinary
geometry (they were already correct). So the point-sprite pipelines were never built in a run; the shaders compile
(SPIR-V and DXIL) and the change is inert until a point list arrives.

### Left

- Point-sprite tessellation is unexercised at runtime (no content draws point lists, see above).
- **Cost**: MoltenVK runs tessellated draws as compute + render; stage 1 measured GPU 3.5-14 ms a frame, two 10 s windows
  at 58.0-58.6 fps when the screen was busy (render thread up to 13 ms). Later, denser stages are untested.
- `FUTARI_PREBUILT_GPU_DIR` (the futari-linux prebuilt-header route) has no prebuilt `rectangle_list_hs/ds` headers.
- Only stage 1's first ~25 s were seen; no boss, no other mode.

## Cost of the tessellation form (2026-11-04)

The user's Black Label (kiniro_normal) play on Renut showed 41-55 fps with the render thread at 21-24 ms and the GPU at
20-22 ms a frame. Measured on the scripted route, every run with a fresh `--user_data_root` under `out/fresh/`:

- **Dev cvars (49102ce)**: `--renut_rect_fallback=plain` (init-only; default `tess`) keeps the one-triangle form on a
  device without geometry shaders. The picture is wrong, but the draws and state are the same. `--renut_draw_timing=true`
  adds a "[renut] draw timing" line every 10 s: render-thread ms per frame in `DrawStateLocked`, pipeline binds, draw
  commands and queue submits, plus draw, rectangle-list and bind counts.
- **Black Label route** from a fresh save:
  `15000:a,17000:a,19000:a,27000:down,28000:down,29500:a,32000:a`, then `start`/`a` pairs every 5 s from 37000 to 69500,
  then `x`, `y`, `rb`, `a` every 2 s from 72000. The first a's answer the new-save prompts, which appear on some runs.
  The two downs pick Black Label on the TOP MENU, then the Black Label Arcade card. Stage 1 runs from about 90 to 105 s.

Busiest 10 s window of each run (same route, same moment):

| run | rect lists / frame | fps | render thread ms | of it in submits | GPU ms |
|---|---|---|---|---|---|
| Ver1.5 st.1, tess (`futari_027`) | 228 | 60.0 | 3.84 | 3.58 | 12.51 |
| Ver1.5 st.1, plain (`futari_028`) | 231 | 60.0 | 0.68 | 0.40 | 0.58 |
| Ver1.5 st.1, tess (`futari_027`) | 175 | 57.3 | 6.92 | 6.72 | 10.02 |
| Black Label st.1, tess (`futari_031`) | 245 | 55.2 | 12.46 | 12.19 | 14.08 |
| Black Label st.1, plain (`futari_032`) | 233 | 60.0 | 0.62 | 0.40 | 0.54 |
| Black Label st.1, tess + `MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS=0` (`futari_033`) | 241 | 56.0 | 0.26 | 0.00 | 13.80 |

How the time splits:
- **Renut's own per-draw work** is the same in both forms and small: `DrawStateLocked` takes 0.1-0.2 ms a frame, binds
  0.00 and draw commands 0.01.
- **Render thread**: almost all of the extra time is the queue submit. MoltenVK encodes the Metal commands there, and for
  each tessellated draw it adds a compute pass and a render-encoder split: about 16-50 µs of CPU per rectangle-list draw.
  With asynchronous submits MoltenVK does that encoding on its own thread, and the render thread falls to 0.26 ms.
- **GPU**: about 55 µs per tessellated draw, against about 2 µs plain. This cost does not move with submit mode. At the
  user's 20-22 ms that is roughly 350-400 rectangle-list draws a frame.
