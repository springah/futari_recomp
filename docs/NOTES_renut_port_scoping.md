# Scoping: porting futari-linux's native renderer (Renut) into futari_recomp

Date: 2026-11-03. Status: SCOPING ONLY, nothing ported. Supersedes the 11-02 "don't port" scoping where the
facts differ; the 11-02 ruling itself is the user's to revisit.

Trigger: issue #8 (Scaloup, 11-02): "game feels too slow compared to my Xbox 360, framerate stable at 60".
The SDK's emulated Xenos path is the cost (cerroverb's Deck numbers on #6: CPU 324 % vs 23 % of a core,
GPU 35 % vs 16 %). Nobody publishes a Windows or macOS binary with the native renderer: futari-nx ships a
Switch NRO only (1.0.4), futari-linux ships Linux x86-64 + AArch64 only (1.2.1). A Windows + macOS build of
that renderer is a real gap, and futari_recomp is the obvious home for it.

Sources read (shallow clones, 11-03): cerroverb/futari-linux @ deca8dd (2026-11-01), ChanseyIsTheBest/futari-nx
@ 917025a (1.0.4). Both GPL-3.0 at the repo level.

## 1. What "the futari-linux renderer" actually is

futari-nx carries THREE renderers, chosen by `futari_renderer` at boot:

| Renderer | Where | Size | Role |
|---|---|---|---|
| `xenos` (SDK emulated GPU) | SDK plugin | - | what futari_recomp ships |
| native PM4-ring renderer (`futari_renderer_native`) | `app/src/me/` | ~44 k lines (futari_native_draws 13.9 k, futari_native_targets 9.7 k, me_native_system 4.5 k) | replaces EDRAM/PM4 under the SDK's graphics system; Switch-era work; NOT what Linux ships |
| **Renut** (`futari_renderer = "renut"`) | `app/src/renut_engine/` | 105 files, ~35.7 k lines (render/draw.cpp 6.1 k, d3d/hooks/* 5 k, backend/* 3.5 k) | Direct3D-level: hooks the game's own D3D9 wrapper calls and draws through plume. No PM4 ring, no EDRAM model, no register file. **This is what futari-linux ships and what cerroverb measured.** |

Renut lineage: masterspike52/reNut -> banjo-nx's adaptation -> futari-nx (2026-10-19/20, one night to first
light on PC, per their notes) -> futari-linux (Vulkan via SDL, display-locked vblank, present_wait).

Only Renut is in scope. The `me/` native renderer and its 44 k lines are NOT needed.

## 2. How Renut attaches to the game (and how that maps to our tree)

- **Hooks by name.** `futari_d3d_functions.txt` maps 74 names to default.xex addresses (block 0x82096B28..),
  found with `d3d_match` against N&B's named XDK and 25 checked by hand. 354 Renut hook lines in
  `gpu/d3d/hooks/{device,draw,resource,shader,state}.cpp`; 70 have a Futari function. The hook macro is
  `REX_HOOK_RAW(sub_X)` overriding the codegen's weak `sub_X`. **Our SDK has the same macro and our generated
  code emits `__attribute__((weak, noinline))`**, so the override mechanism works unchanged in futari_recomp.
- **The eight cores.** Each core links its own D3D9 subset. `tools/renut_futari_hooks.py` finds each core's
  copy by instruction matching (45 of 70 present in every core; 38 hooked) and writes
  `futari_renut_core_hooks.cpp`, compiled into every core; a core forwards to the exe's entry table via
  **GetProcAddress on the PC** (`FutariRenutEntries`). Our cores are already SHARED libraries with hidden
  visibility (`generated/default/dll_targets.cmake`), so this is the same shape; `scripts/regen.sh` must learn to
  add one app-side source per module (the same change the 11-02 scoping named for dt_plume).
- **7 addresses are futari-nx's own CPU natives** (BeginVertices, AllocateInlineData, SetSurfaceClip, Clear,
  Resolve, KickOff, CBlocker_Check) that yield to Renut. We have none of those natives, so Renut hooks those
  addresses directly: simpler for us.
- **Vertical blank.** Renut has no GPU, so a thread of its own calls Direct3D's vblank handler `sub_820A4C08`
  at the display's rate (`futari_renut_vblank.cpp`, 171 lines; PC: sleeps to 1.5 ms before each vblank then
  yields). The game's frame counter at 0x82200560 advances through it. This REPLACES our present_vsync pacing.
- **Boot integration** (`futari_app.h`, ~20 lines): if Renut is active, `ConfigureRuntime`, start the vblank
  thread, Renut draws the SDK's ImGui overlay through its own plume drawer, handles resize, and the xenos
  plugin is still built (`GPU_PLUGINS xenos`) as the fallback. Our `src/main.cpp` (798 lines, no hooks today)
  gets the same ~20-line surface.
- **Device layout.** Futari's XDK 2.0.9328 device offsets differ from N&B's by region (+0xAC/+0xB0/+0xC8/+0xCC/
  +0xD0/+0xD4); already translated in their tree (`tools/renut_futari_layout.py` + hand fixes). Comes for free.

## 3. SDK dependency: the one real seam

futari-linux's SDK is a fork of rexglue-sdk **v0.10.0** (205 files, +22 k lines; 32 k-line patch). Almost all of
it is the Switch/Horizon layer, codegen options and console tuning. What Renut itself needs beyond upstream:

| Fork change | Size | Needed by Renut? |
|---|---|---|
| `include/rex/frame_latency.h` + `src/core/frame_latency.cpp` (`rex::latency::SwapCount/NoteGuestSwap/NoteGpuDone/NoteDisplayVsync/InputTimeOf/Report`) | 55 + 214 lines, NEW | yes, 8 call sites (display_sync, vblank, video, tuning, device hook) |
| `include/rex/hook.h` | +13 | probably (check which macro) |
| `include/rex/ui/presenter.h` / `src/ui/presenter.cpp` | +102 / +387 | unclear: Renut has NO presenter use in its own code; likely the SDK's swap counting / capture path |
| `include/rex/kernel/xboxkrnl/video.h` | +8 | yes (VdSwap feeds SwapCount) |
| `include/rex/cvar.h` | +3 | trivial |
| `resources/templates/codegen/dll_targets_cmake.inja` | +9/-2 | app-side source per module (we do this in regen.sh instead) |
| `src/ui/vulkan/vulkan_presenter.cpp` | +592 | NO (SDK presenter; Renut presents through plume itself) |

**Leg 1 result (11-03, branch `renderer/renut`).** Measured against the fork's shipped `sdk/` tree with CRLF
noise removed, the drift in the 26 headers Renut includes is ~500 lines, and Renut references exactly ONE symbol that
exists only in v0.10.0: `rex::memory::MarkStoreCardsAt` (a no-op in Futari). Of the fork deltas in the table, Renut
needs frame_latency (new, verbatim), the video.h trio (`GuestSwapCount` reads the latency probe's counter; `VdSwap`
calls `NoteGuestSwap`, so a renderer that replaces Swap keeps the count honest), and from input_system the late-read
block + `SetFrameReadDelayFunction` (Renut's render_thread.cpp installs its `LateInputDelay`). NOT needed and NOT
ported: hook.h (`REX_EXPORT_CENSUS`, sys_counters), cvar.h (`LoadConfigString`), the presenter own-thread work
(Switch), kernel_state static modules, the ppc/context.h diet. Ported as 6 edited files + 2 new, 385 lines.

Every other header Renut includes (`rex/cvar.h`, `types.h`, `hook.h`, `logging.h`, `ppc.h`, `system/*`, `ui/*`,
`platform.h`, `thread.h`, `runtime.h`, `filesystem.h`) **exists in our vendored v0.9.3.1**. Two routes:
(a) port the ~800 lines above onto our v0.9.3.1; (b) bump the vendored SDK to v0.10.0 first (cleaner, and the
bump is owed eventually anyway, but v0.9.3.1 -> v0.10.0 is its own regression risk: present_vsync fix d942aec,
title-exit, mac seal). Recommend (a) first, (b) later.

## 4. Coupling to the rest of futari-nx (what to shim)

Non-SDK includes from renut_engine: `me/me_platform_compat.h` (4, Switch thread helpers), `cpu/banjo_enhancements.h`
(3, the enhancement master switches), `native/futari_native.h` (2), `profiler/profiler.h` (1),
`me/masseffect/banjo_pmull_fingerprint.h` (1, texture hash), imgui. Each is a small header; a ~200-line
`futari_renut_shim` replaces them (their own `futari_renut_shim.cpp` already isolates 52 cvars).
deko3d (Switch) is mentioned in 31 files / 363 lines: strip or `#if`-out.

**Leg 2 result (11-03).** Done in the same session as leg 1 with three parallel agents (shim headers; CMake wiring +
configure; hook-mechanics analysis). Facts that corrected this document:
- deko3d needs NO stripping: every mention is behind `PLUME_DEKO3D` / `REX_PLATFORM_SWITCH` (we define the latter 0 in
  platform.h). Linux-only surface code is behind `!_WIN32`. Nothing in Renut assumes Switch or Linux unguarded.
- Six shim headers, not five: `futari_enhancements.h` too (`futari_renut_shim.cpp` includes it). Shims live in
  `src/renut_shim/` on the include path; Renut's sources are untouched except gpu.cmake (3 tagged edits). Six cvars
  Renut only DECLAREs (`futari_vblank_*`, `futari_late_input`) and the Switch perf-counter stubs are defined in
  `src/renut_shim/renut_shim_stubs.cpp`.
- The hooks are safe to link unconditionally: there is no `REX_HOOK_RAW` in Renut; its `FUTARI_RENUT_HOOK` sites test
  `banjo::renut_gpu::Active()` (latched once from `futari_renderer`) and otherwise call the codegen's `__imp__sub_X`
  body, the same override pattern main.cpp already uses for the title exit. Verified: the xenos boot log with the
  renderer linked is identical to the leg-1 log.
- The shipped `futari_renut_hooks.h` / `futari_renut_core_hooks.cpp` carry futari-nx's 7 "yield to native" EMPTY sites
  (BeginVertices, Clear, Resolve, CBlocker_Check, AllocateInlineData, KickOff, SetSurfaceClip). They must be regenerated
  with `tools/renut/renut_futari_hooks.py` against our generated/ before the first Renut boot (leg 3); the script needs a
  small path patch (expects tools/<script> and app/ layout, and opens futari-nx's futari_native_core.cpp
  unconditionally). All 70 mapped addresses exist as weak `sub_X` in our generated/default.
- ImGui: the SDK's 1.92.5 copy is compiled a second time into the exe (no vendored copy). plume builds as-is from
  futari-linux's tree (Vulkan=1, D3D12=1 on Windows); the daytona-fork reconcile is deferred until something needs it.
- dxc: XenosRecomp's dxc-bin submodule ships no binaries; `FUTARI_DXC` finds `_tools/XenosRecomp/.../dxc.exe`
  (1.8.2407) or the Windows SDK's. The REAL shader-cache pipeline (option OFF) also needs dxcompiler in dxc-bin's own
  folders on Windows, a leg-4 item.
- Boot surface for main.cpp (leg 3): 7 ReXApp overrides gated on `Active()`, in SDK order: OnPostInitLogging (gpu_plugin
  default) -> OnPreSetup `renut::gpu::ConfigureRuntime(config)` (detached mode, no SDK graphics) -> OnCreateImmediateDrawer
  `CreateOverlayDrawer()` -> OnFinalizePaths `StartRenderer(app_context(), imgui_drawer getter)` -> OnPostSetup
  `futari::renut_vblank::Start()` -> OnWindowPixelSizeChanged `OnWindowResized()` -> OnShutdown `Stop()` +
  `ShutdownRenderer()`. futari_linux.toml non-defaults: `futari_renderer = "renut"`, `renut_gpu_backend = "vulkan"`.
  No cvar name collides with main.cpp's or the SDK's. Full detail: `docs/NOTES_renut_hooks_analysis.md`.

## 5. plume and XenosRecomp

- plume: their `app/thirdparty/plume` vs our springah/plume fork (daytona, d3d12 fixes) differ in 26 paths, of
  which only `plume_d3d12.cpp`, `volk.c` and `CMakeLists.txt` matter. Reconcile once; both have
  `plume_metal.cpp` / `plume_apple.mm`.
- Renut's shaders go through **reblue-XenosRecomp** (their MIT fork, built as an ExternalProject at build time,
  needs dxc). Host shaders are prebuilt SPIR-V/DXIL headers. We have XenosRecomp + dxc in `_tools/` already.

## 6. Backends: Windows fine, macOS is the unknown

Renut includes `plume_vulkan.h` / `plume_d3d12.h` directly in 8 files (backend.cpp, native_window.cpp,
present.cpp, video.cpp, record.cpp, tuning.cpp, render/draw.cpp, deko3d.cpp) and mentions Metal **zero** times.
- **Windows**: futari-nx's own PC dev build ran Renut on plume Vulkan at first light (2026-10-20: title, menus,
  gameplay in all core DLLs, render thread 0.3 ms, GPU 0.4-0.7 ms/frame, 96 pipelines) and D3D12 is in gpu.cmake.
  futari-linux calls the Windows build "untouched and untested here", meaning untested by cerroverb, not broken.
- **macOS**: no Metal path in Renut. Cheapest route = plume's Vulkan backend over MoltenVK (plume_apple.mm does
  the surface); `VK_KHR_present_wait` is optional in their code ("off, the driver has no present_wait").
  Risk: unknown until tried. Budget it as its own leg after Windows.

## 7. Shaders are game data: the shipping model

Renut's cache is built from `tools/renut_dump_shaders.py`'s dump of the NINE loaded images (42 containers:
default.xex 22 VS + 22 PS, each core 2 + 2, the cores' four identical). Translated game shaders in the binary
would break futari_recomp's no-bundled-game-data rule. futari-linux already solved this the way we need:
`FUTARI_LINUX_RELEASE_SHADERS=ON` ships NO game shader; `vulkan_spirv.bin` is made on the player's machine from
their own executables (their web installer page runs XenosRecomp + DXC in WebAssembly). For us the natural
seat is the existing extractor step (`extract_assets.bat` / the wizard already runs on the user's dump): run
dump + XenosRecomp + dxc there and write the cache beside the exe. Windows D3D12 needs DXIL as well as SPIR-V;
**Vulkan-only on Windows for the first cut** keeps it to SPIR-V.

## 8. Licence: the gate that is not engineering

- futari_recomp is BSD-3. futari-nx / futari-linux `app/` (which includes renut_engine and every adaptation) is
  **GPL-3.0**. Linking Renut makes futari_recomp's binaries GPL-3 (source can stay BSD for our files, but the
  distributed work is GPL). GPL already grants the reuse; the consequence is the relicensing of the shipped build.
- **reNut itself has NO licence file.** Their notice says "its author permits the user's [ChanseyIsTheBest's]
  ports to reuse it", which read as personal. **Asked and GRANTED 2026-11-03** (masterspike52/reNut issue #74):
  "in my readme it says you are free to fork it. i have no problems with anyone doing such for any reason." The
  README indeed carries a blanket free-to-fork line. No LICENSE file was added, so our NOTICES/README cite the
  issue and the README line, and credit masterspike52 / reNut as the other ports do. No longer blocking.
- plume MIT, XenosRecomp MIT (+ their MIT-licensed patches), SDK BSD-3: no issue.

## 9. Effort estimate (sessions), Windows first

| Leg | What | Est. |
|---|---|---|
| 0 | Licence: reNut permission GRANTED (issue #74, 11-03); decide GPL-3 for the renderer build | done |
| 1 | SDK deltas onto v0.9.3.1: frame_latency, video.h, input late-read, MarkStoreCardsAt (116 lines + 269 new; hook.h / presenter / cvar deltas NOT needed, see §3) | done 11-03 (branch `renderer/renut`) |
| 2 | Vendor renut_engine + plume + reblue-XenosRecomp; shim the futari-nx headers; compile + link on win-amd64 (stub shader cache); xenos still boots | done 11-03 (same session as leg 1, 3 agents) |
| 3 | Hooks: `futari_d3d_functions.txt` + `renut_futari_hooks.py` as-is; regen.sh adds the core hook source per DLL; vblank thread; boot surface in main.cpp | 1 |
| 4 | Shader cache at extract time (dump -> XenosRecomp -> dxc SPIR-V), `vulkan_spirv.bin` beside the exe; Vulkan-only | 1 |
| 5 | First light + gates on Windows: lockstep vs xenos (their `compare_frames.py` / lockstep scripts exist), all 9 images, DLC modes, present_vsync parity, F4 overlay through Renut's drawer | 1-2 |
| 6 | macOS via MoltenVK; risk unknown | 1-2 |
| | **Total** | **6-9 sessions** + licence wait |

Compare the 11-02 estimate for porting daytona's dt_plume: 3-4 WEEKS of sessions. Renut is already proven on
this exact game, with the device layout, the 70-function map and the per-core hook finder done. That is why
this is cheaper than the 11-02 scoping assumed.

## 10. Recommendation

Do it, as `futari_renderer = "renut"` opt-in behind the xenos default until gated, Windows Vulkan first.
Sequence: licence email today (leg 0) -> legs 1-3 in one branch (`renderer/renut`) -> leg 4 -> gate -> ship as
v1.6.0 with xenos still selectable (fallback, A/B without rebuild). Answer #8 with that plan rather than a
settings hunt; the "feels slow" complaint is most likely the frame latency of the emulated path plus host
vsync, which Renut's display-locked vblank and 2-image swapchain were built to remove.

Not in scope: the `me/` native PM4 renderer, banjo's CPU natives / codegen rounds, Switch anything, the
installer web page (our extractor does that job).
