# Renut hooks, cores, boot surface: read-only analysis of the vendored tree

Date: 2026-11-03. Branch `renderer/renut`. Read against `src/renut_engine/` (futari-linux @ deca8dd copied verbatim:
`diff -rq` shows only `gpu/gpu.cmake` edited plus our `LICENSE` / `PROVENANCE.md`; `tools/renut/*.py` identical to
futari-linux `tools/`), `generated/`, `thirdparty/rexglue-sdk` v0.9.3.1 + leg 1, and the read-only reference clones
of futari-linux / futari-nx. Companion to `docs/NOTES_renut_port_scoping.md` §2.

## Summary

- **(a) Fallback: YES, the game boots and draws through xenos when `futari_renderer != "renut"`.** Renut's hook
  sources contain **no `REX_HOOK_RAW` at all** (0 sites); every one of the 356 hook lines is `FUTARI_RENUT_HOOK` /
  `FUTARI_RENUT_HOOK_RAW` (`gpu/banjo_renut_select.h:37-50`), whose per-name site macro (generated into
  `futari_renut_hooks.h`) is a strong `sub_X` that tests `banjo::renut_gpu::Active()` and otherwise calls
  **`__imp__sub_X`**, the codegen's real body. The mechanism is the SDK/codegen one: `DEFINE_REX_FUNC(sub_X)` emits the
  body as `__imp__sub_X` and `sub_X` as a `weak` alias of it (`generated/default/futari_init.h:65-67`); a strong
  `sub_X` elsewhere wins the link and the function table binds to it. `main.cpp:791-796` already uses exactly this for
  `sub_821CBFF0`. `Active()` latches `futari_renderer == "renut"` once (`banjo_renut_select.cpp:28-45`).
  **Caveat that matters for the renut path, not the xenos path:** 7 sites are EMPTY in the shipped header
  (BeginVertices, Clear, Resolve, KickOff, AllocateInlineData, SetSurfaceClip, CBlocker_Check) because futari-nx's own
  CPU natives owned those addresses and yielded; we have no such natives, so the generator must be re-run so they become
  real sites (it will, since it finds no owner in our `src/`).
- **(b) Cores: a core DLL compiles `futari_renut_core_hooks.cpp` (needs `FUTARI_RENUT_RENDERER=1` on that source and a
  target-wide `FUTARI_CORE_<name>`), which includes `futari_renut_core.h` + `futari_renut_hooks.h` + `<windows.h>`, and
  resolves `GetProcAddress(GetModuleHandleW(nullptr), "FutariRenutEntries")` and `"FutariRenutCallWithFree"` once.**
  Those two are exported from the exe by `extern "C" __declspec(dllexport)` in `banjo_renut_select.cpp:52-78`
  (no `.def`, no `WINDOWS_EXPORT_ALL_SYMBOLS`, no `ENABLE_EXPORTS`; Linux uses `--export-dynamic-symbol=Futari*`). Our
  generated cores use the same weak-alias `DEFINE_REX_FUNC` (`generated/palm_normal/futari_init.h:57-67`), so the core
  hook macro `REX_HOOK_RAW(sub_<addr>)` + `__imp__sub_<addr>` fallback works unchanged. The shipped core file also
  carries the 7 "yields" (56 lines across 8 cores) that must be regenerated into real hooks. `cmake/futari_renut_gpu.cmake`
  already wires this behind `FUTARI_RENUT_CORE_HOOKS` (OFF today).
- **(c) Addresses: all 70 mapped `sub_<ADDR>` exist in `generated/default/*.cpp` as `DEFINE_REX_FUNC` (weak).**
  Swap `820A5408` (`futari_recomp.2.cpp:26860`), DrawVerticesUP `8209DC98` (:8722), CreateDevice `820A4AC0` (:25395),
  SetTexture `8209D660` (:7798), KickOff `820A8FC0` (:36242); vblank handler `820A4C08` (:25602). The generator
  `tools/renut/renut_futari_hooks.py` takes the name map, the renderer sources (rewritten in place, idempotent), the
  generated code dir (`--gen`), and futari-nx's `app/src/native/futari_native_core.cpp`; it writes `futari_renut_hooks.h`
  and `futari_renut_core_hooks.cpp`. **It cannot run against this tree as-is**: its `ROOT`/`APP`/`ENGINE` paths assume
  `tools/<script>` + `app/src/renut_engine` (we have `tools/renut/<script>` + `src/renut_engine`), and it opens the
  non-existent `futari_native_core.cpp` unconditionally (FileNotFoundError). Three-line path fix + "no core natives"
  guard, then: `python3 tools/renut/renut_futari_hooks.py --gen generated --check` / without `--check`.
- **(d) Boot surface = 7 overrides, ~25 lines**: `OnPreSetup` -> `renut::gpu::ConfigureRuntime(config)` (clears
  `graphics` + `gpu_plugin` = SDK detached mode); `OnCreateImmediateDrawer` -> `renut::gpu::CreateOverlayDrawer()`;
  `OnFinalizePaths` -> `renut::gpu::StartRenderer(app_context(), [this]{ return imgui_drawer(); })`; `OnPostSetup` ->
  `futari::renut_vblank::Start()`; `OnWindowPixelSizeChanged` -> base + `renut::gpu::OnWindowResized()`; `OnShutdown`
  -> `futari::renut_vblank::Stop(); renut::gpu::ShutdownRenderer()`. All gated on `banjo::renut_gpu::Active()`. The SDK
  v0.9.3.1 has every one of these virtuals and the detached-mode branch (`rex_app.cpp:385-393`). xenos stays built
  (`rexglue_setup_target(futari GPU_PLUGINS xenos)` in both trees). The SDK's ImGui overlay is drawn by Renut's plume
  `OverlayDrawer`; there is no SDK presenter (so `present_vsync` is inert under Renut; `renut_vsync` applies).
- **(e) cvars: no collisions.** Renut defines 142 cvars (24 files); `src/main.cpp` defines 0; the SDK 224;
  renut x SDK, renut x main, main x SDK intersections are all empty, and no name is defined twice inside renut_engine.
  Renut `REXCVAR_DECLARE`s 24 externs: 18 are defined inside renut_engine, 6 are futari-nx's (`futari_vblank_sleep_exact`,
  `futari_vblank_display_sync`, `futari_vblank_delay_us`, `futari_vblank_phase_auto`, `futari_vblank_phase_max_us`,
  `futari_late_input`); `src/renut_shim/renut_shim_stubs.cpp:18-53` defines them, **but that file is not in
  `cmake/futari_renut_gpu.cmake`'s source list yet** (link error until added). Duplicates are not fatal in this SDK
  (`cvar.cpp:192`: error log, second ignored). `futari_linux.toml` non-defaults: `futari_renderer = "renut"`,
  `renut_gpu_backend = "vulkan"` (default `"d3d12"`), `fullscreen = true`, `log_level = "warn"`; `futari_fill_crop =
  false` and `renut_vsync = false` equal their defaults; `futari_renderer_native = true` is a futari-nx `me/` cvar we do
  not have (would log "unknown cvar").
- **(f) Beyond the five shim headers**: a 6th header `futari_enhancements.h` (`futari_renut_shim.cpp:10`, shimmed with
  `GroupOn -> false`, i.e. every `banjo_*` enhancement off: a choice to revisit); a dead `renut_logging.h` including
  `generated/renut_init.h` (nobody includes it: delete); `native/futari_native.h` symbols `CodeHash` +
  `CodegenChecksReport` (shimmed); the 6 external cvars above; Switch extern-"C" SDK symbols `RexSetGuestThreadCoreLayout`
  (under `#if REX_PLATFORM_SWITCH`), `RexSwitchPerfCount` (unguarded call, stubbed in `renut_shim_stubs.cpp:61-62`),
  `RexStoreCardsHostBytes` (defined in the shim .cpp). No `futari_app.h`, no `futari_native_core`, no census code (only
  comments). deko3d/Switch compile-time code is behind `PLUME_DEKO3D` / `REX_PLATFORM_SWITCH` / `__SWITCH__`; the
  unguarded mentions are run-time `IsDeko3D()` branches and help text. Linux-only Wayland/X11 surface code is behind
  `#if !defined(_WIN32) && !defined(__SWITCH__)` and `PLUME_SDL_VULKAN_ENABLED`; Windows-only includes are behind
  `_WIN32`. SDL3 is used unguarded off-Switch in `futari_renut_vblank.cpp` (fine: the SDK's Windows window is SDL3).

## (a) Runtime selection and the fallback

### No REX_HOOK_RAW in the renderer

```
$ grep -oh "\b[A-Z_]*HOOK[A-Z_]*\b\|\bRENUT_[A-Z_]*\b" src/renut_engine/gpu/d3d/hooks/*.cpp src/renut_engine/futari_renut_hooks.h | sort | uniq -c
    356 FUTARI_RENUT_HOOK
```

Per file: device.cpp 32, draw.cpp 16, resource.cpp 45, shader.cpp 6, state.cpp 255. The only `REX_HOOK_RAW` text in
`src/renut_engine` is inside the core macro definitions (`futari_renut_core.h:67,79`). `hooks_common.h` defines
`RENUT_NOOP` / `RENUT_NOOP_RETURN` as plain `extern "C" REX_FUNC(subroutine)` bodies (lines 13-24) but **nothing uses
them** (0 sites). The upstream tool rewrote banjo-nx's `BANJO_RENUT_HOOK(sub_X, fn); // Name` into
`FUTARI_RENUT_HOOK(Name, fn); // N&B sub_X` (`tools/renut/renut_futari_hooks.py:41-44, 80-92`); e.g.
`gpu/d3d/hooks/device.cpp:1293`:

```cpp
FUTARI_RENUT_HOOK(D3DDevice_Swap, D3DDevice_Swap_hook);  // N&B sub_82287590
```

### The macros (gpu/banjo_renut_select.h:37-50)

```cpp
#define FUTARI_RENUT_HOOK(name, function)                                              \
  extern "C" void futari_renut_entry_##name(PPCContext& ctx, uint8_t* base) {          \
    ::rex::ppc::HostToGuestFunction<function>(ctx, base);                              \
  }                                                                                    \
  FUTARI_RENUT_SITE_##name

#define FUTARI_RENUT_HOOK_RAW(name)                                                    \
  static void futari_renut_raw_##name(PPCContext& ctx, uint8_t* base);                 \
  extern "C" void futari_renut_entry_##name(PPCContext& ctx, uint8_t* base) {          \
    futari_renut_raw_##name(ctx, base);                                                \
  }                                                                                    \
  FUTARI_RENUT_SITE_##name                                                             \
  static void futari_renut_raw_##name([[maybe_unused]] PPCContext& __restrict ctx, uint8_t* base)
```

So every hook defines an **entry** `futari_renut_entry_<Name>` (always linked, called from the default.xex site, from
the cores' table, and from a yielding native hook) and then expands the **site** macro for that name from
`futari_renut_hooks.h` (included at `banjo_renut_select.h:33`).

### The site macro: the fallback (futari_renut_hooks.h:805-810, D3DDevice_Swap)

```cpp
#define FUTARI_RENUT_SITE_D3DDevice_Swap \
  REX_EXTERN(__imp__sub_820A5408); \
  extern "C" REX_FUNC(sub_820A5408) { \
    if (::banjo::renut_gpu::Active()) { futari_renut_entry_D3DDevice_Swap(ctx, base); return; } \
    __imp__sub_820A5408(ctx, base); \
  }
```

Same shape for CreateDevice (`:1019-1024`, `sub_820A4AC0`), BlockOnFence (`:390-395`), CreatePixelShader (`:405-410`),
etc. Emitted by `renut_futari_hooks.py:257-263`. Three site shapes exist in the header:

| Shape | Example | Meaning |
|---|---|---|
| real site (strong `sub_X`, `__imp__` fallback) | `:805-810` Swap | the game function is intercepted while renut is chosen, the original runs otherwise |
| empty: `// not in Futari's Direct3D` | `:368` GetArraySize, `:437` DrawIndexedVertices | Renut has a hook but Futari's D3D has no such function; the entry exists, nothing calls it |
| empty: `// sub_X: Futari's own hook (...) yields` | `:389` BeginVertices `sub_8209D800`, `:397` Clear `sub_820A19F0`, `:597` Resolve `sub_820A2280`, `:901` CBlocker_Check `sub_820A4590`, `:902` AllocateInlineData `sub_820A8138`, `:916` KickOff `sub_820A8FC0`, `:997` SetSurfaceClip `sub_82096B28` | futari-nx's own natives owned the address and called `FUTARI_RENUT_YIELD(Name)` first thing (`futari_renut_yield.h:9-13`). **We have none of those natives**, so in this tree these 7 functions are hooked by nobody: xenos path unaffected, renut path broken until regenerated (see (c)). |

### Why the strong `sub_X` + `__imp__sub_X` pair works: the codegen convention

`generated/default/futari_init.h:52-67` (identical in every core's `futari_init.h`):

```cpp
#define DECLARE_REX_FUNC(name) \
  REX_EXTERN(name);            \
  REX_EXTERN(__imp__##name)

#if defined(__APPLE__)
#define DEFINE_REX_FUNC(name)                                                       \
  extern "C" void __imp__##name(PPCContext& __restrict, uint8_t*);                  \
  __attribute__((weak, noinline)) extern "C" void name(                             \
      [[maybe_unused]] PPCContext& __restrict ctx, uint8_t* base) {                 \
    __imp__##name(ctx, base);                                                       \
  }                                                                                 \
  extern "C" void __imp__##name([[maybe_unused]] PPCContext& __restrict ctx, uint8_t* base)
#else
#define DEFINE_REX_FUNC(name)                                  \
  __attribute__((alias("__imp__" #name))) REX_WEAK_FUNC(name); \
  REX_EXTERN(__imp__##name)
#endif
```

with `REX_FUNC` / `REX_EXTERN` / `REX_WEAK_FUNC` from `thirdparty/rexglue-sdk/include/rex/ppc/context.h:50-52`
(`REX_WEAK_FUNC(x) = __attribute__((weak, noinline)) REX_FUNC(x)`), and `REX_HOOK_RAW(name) = extern "C" REX_FUNC(name)`
(`include/rex/hook.h:49`). The generated body is always `__imp__sub_X`; `sub_X` is a weak alias (Windows/Linux) or a
weak trampoline (macOS). The function table binds the `sub_X` symbol (`generated/default/futari_init.cpp:1583
{ 0x820A4C08, sub_820A4C08 }`, `futari_register.cpp:1571 registrar->SetFunction(0x820A4C08, sub_820A4C08)`), so a
strong `sub_X` from the hook replaces the dispatch entry and keeps the original reachable as `__imp__sub_X`. This is
precisely what `src/main.cpp:786-796` does today for the title-exit wrapper (comment: "Generated guest functions are weak
aliases of __imp__sub_*, so this strong definition takes over the dispatch entry without any codegen change").

Answering the question's list: it is not a renamed copy, not a pointer captured at startup, not the SDK's indirect
table; it is the **`__imp__sub_X` symbol** the codegen emits for every function (the same thing the SDK's
`REX_IMPORT(__imp__sub_X, ...)` targets, e.g. `device.cpp:76`, `resource.cpp:52`).

### Active()

`gpu/banjo_renut_select.h:20-29`: `Active()` reads an `int8_t g_active` (1 renut, -1 not, 0 unread) and calls
`detail::LatchActive()` once. `gpu/banjo_renut_select.cpp:15-22` defines the cvar:

```cpp
REXCVAR_DEFINE_STRING(futari_renderer, "default", "Futari", "...")
    .allowed({"default", "renut"})
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
```

and `:28-45` latches `REXCVAR_GET(futari_renderer) == "renut"` (the `PLUME_DEKO3D` block inside is Switch-only). The
first `Active()` call must come after the SDK has loaded the config: the SDK loads it in `SetupEnvironment`
(`rex_app.cpp:150-183`, `LoadConfig` before `OnPostInitLogging`), and the earliest Renut call is `OnPreSetup`
(`SetupPresentation`, `rex_app.cpp:322`), so the order is right. Hooks run far later (guest code).

### Verdict for (a)

With `futari_renderer` left at `"default"`: every real site calls `__imp__sub_X` = the recompiled body; empty sites
change nothing; the entries and the renderer objects are linked but idle; `FutariRenutEntries()` returns `nullptr`
(`banjo_renut_select.cpp:63-66`) so core hooks also fall through to `__imp__`. Nothing else in the renderer runs
unless the app calls `ConfigureRuntime` / `StartRenderer` / vblank `Start`, all of which `futari_app.h` gates on
`Active()`. **The xenos path is intact.** Two residual link-time dependencies exist regardless of the cvar:
`REX_IMPORT(__imp__sub_8208DA10, GuestGetCurrentThreadId, u32())` (`device.cpp:76`) and
`REX_IMPORT(__imp__sub_82088CE0, DefaultXMemFree, void(u32, u32))` (`resource.cpp:52`); both are defined in
`generated/default/futari_recomp.1.cpp` (`DEFINE_REX_FUNC(sub_82088CE0)` at :25970; `sub_8208DA10` also in that file).

## (b) The cores

### What a core DLL needs (src/renut_engine/futari_renut_core.h)

- Includes (`:8-28`): `<cstdint>`, `<rex/hook.h>`, `<rex/ppc.h>`, `"futari_renut_hooks.h"` (same directory, for
  `FUTARI_RENUT_CORE_INDEX_<Name>` and `FUTARI_RENUT_CORE_COUNT`), and on `_WIN32` `<windows.h>` (`WIN32_LEAN_AND_MEAN`,
  `NOMINMAX`); off Windows it declares `FutariRenutEntries` / `FutariRenutCallWithFree` as `visibility("default")`
  externs resolved by the dynamic linker (`:23-28`).
- Lookup (`:40-60`), once per DLL, in a function-local static:

```cpp
    const HMODULE exe = GetModuleHandleW(nullptr);
    using GetEntries = const Entry* (*)(uint32_t*);
    const auto get = reinterpret_cast<GetEntries>(reinterpret_cast<void*>(GetProcAddress(exe, "FutariRenutEntries")));
    t.entries = get ? get(&count) : nullptr;
    t.call_with_free =
        reinterpret_cast<CallWithFree>(reinterpret_cast<void*>(GetProcAddress(exe, "FutariRenutCallWithFree")));
    ...
    if (count != FUTARI_RENUT_CORE_COUNT || !t.call_with_free) t.entries = nullptr;
```

  `entries == nullptr` means "renut not chosen" (or a table of another build): every core hook then calls the original.
- The hook macros (`:65-86`):

```cpp
#define FUTARI_RENUT_CORE_HOOK(address, name)                                          \
  extern "C" REX_FUNC(__imp__sub_##address);                                           \
  REX_HOOK_RAW(sub_##address) {                                                        \
    if (const auto* e = ::futari::renut_core::Get().entries) {                         \
      e[FUTARI_RENUT_CORE_INDEX_##name](ctx, base);                                    \
      return;                                                                          \
    }                                                                                  \
    __imp__sub_##address(ctx, base);                                                   \
  }
```

  and `FUTARI_RENUT_CORE_HOOK_FREE(address, name, free)` (`:76-86`) which routes through
  `t.call_with_free(INDEX, &ctx, base, &__imp__sub_<free>)` so that `D3DResource_Destroy` / `_Release` free with the
  **core's** XMemFree (`g_core_xmemfree` thread_local in `renut::gpu`, `banjo_renut_select.cpp:70-78`,
  `resource.cpp` GameXMemFree). `FUTARI_RENUT_CORE_YIELD(name)` (`:89-93`) is for futari-nx's `futari_native_core.cpp`
  (absent here).

### futari_renut_core_hooks.cpp (394 lines, generated)

- `:6 #if FUTARI_RENUT_RENDERER` ... `:394 #endif`; `:7 #include "futari_renut_core.h"`.
- One block per core: `#if defined(FUTARI_CORE_palm_normal)` (`:10`), `palm_abnormal` (`:58`), `reco_normal` (`:106`),
  `reco_abnormal` (`:154`), `hirow_normal` (`:202`), `hirow_abnormal` (`:250`), `kiniro_normal` (`:298`),
  `kiniro_abnormal` (`:346`). 304 `FUTARI_RENUT_CORE_HOOK*` lines = 38 per core, e.g. `:11`
  `FUTARI_RENUT_CORE_HOOK(88078318, D3DArrayTexture_UnlockRect)  // default.xex sub_8209B710`, `:30`
  `FUTARI_RENUT_CORE_HOOK_FREE(8807F370, D3DResource_Destroy, 88086278)  // default.xex sub_820A3E98`.
- The 7 yields per core, as comments (`:12-13, 21, 38-39, 42, 51` for palm_normal; the same seven in every core):
  `// D3DDevice_BeginVertices: sub_8807ECB0, futari_native_core.cpp's own hook yields`. In this tree these are
  **unhooked** until regenerated (the generator writes real hooks when `owned_cores` is empty).
- The exe side of the table: `futari_renut_hooks.h:1038-1131` `FUTARI_RENUT_CORE_ENTRIES(X)` lists 45 names in index
  order (`X(D3DArrayTexture_UnlockRect) /* 0 */ ... X(XGRegisterVertexShader) /* 44 */`), `FUTARI_RENUT_CORE_INDEX_*`
  and `FUTARI_RENUT_CORE_COUNT 45`; `banjo_renut_select.cpp:57-61` builds `kCoreEntries[]` from it.

### Exported symbols and how upstream exports them (banjo_renut_select.cpp:50-78)

```cpp
#if defined(_WIN32)
#define FUTARI_RENUT_EXPORT extern "C" __declspec(dllexport)
#else
#define FUTARI_RENUT_EXPORT extern "C" __attribute__((visibility("default")))
#endif
...
FUTARI_RENUT_EXPORT const Entry* FutariRenutEntries(uint32_t* count) {
  if (count) *count = FUTARI_RENUT_CORE_COUNT;
  return banjo::renut_gpu::Active() ? kCoreEntries : nullptr;
}
FUTARI_RENUT_EXPORT void FutariRenutCallWithFree(uint32_t index, PPCContext* ctx, uint8_t* base, PPCFunc* xmemfree) { ... }
```

futari.exe must export exactly these two. Upstream has **no** `.def`, **no** `WINDOWS_EXPORT_ALL_SYMBOLS`, **no**
`ENABLE_EXPORTS` (grep of `app/CMakeLists.txt`, `app/cmake/*.cmake`, the SDK template): on Windows the `dllexport` on a
function in an executable is enough for `GetProcAddress(GetModuleHandleW(nullptr), ...)`. On Linux
`app/CMakeLists.txt:712` adds `LINKER:--export-dynamic-symbol=Futari*` (the comment: "the executable exports them (only
them: its generated sub_X stay local)"). Our `cmake/futari_renut_gpu.cmake:167` links `futari_renut_gpu` (an OBJECT
library, `:142`) into `futari`, so the objects and their exports land in the exe without archive-pull issues.

### Our generated cores use the same convention

- `generated/palm_normal/futari_init.h:57-67`: the same `DEFINE_REX_FUNC` weak-alias macro as default's.
- `generated/palm_normal/futari_recomp.0.cpp:22249 DEFINE_REX_FUNC(sub_88078318)` (UnlockRect copy, hooked at
  `core_hooks.cpp:11`), `:37907 DEFINE_REX_FUNC(sub_8807ECB0)` (BeginVertices copy, one of the 7 to regenerate),
  `:57902 DEFINE_REX_FUNC(sub_88086278)` (the core's XMemFree, the `free` argument at `core_hooks.cpp:30,32`).
- File names `futari_recomp.N.cpp` match the generator's glob `*_recomp.*.cpp` (`renut_futari_hooks.py:163`).
- Build shape: `generated/default/dll_targets.cmake:25-34` (written by `scripts/regen.sh:97-110`):
  `add_library(futari_palm_normal SHARED ...)`, include dirs `${CMAKE_CURRENT_SOURCE_DIR}` + `generated/palm_normal`,
  `rex::runtime`, `CXX_VISIBILITY_PRESET hidden`. Hidden visibility is irrelevant to the override: the strong
  `sub_8807xxxx` and the weak alias live in the same DLL and the core's own `futari_register.cpp` table binds the
  strong one. Upstream puts `FUTARI_CORE_<name>` on the core target (`app/CMakeLists.txt:486`, and
  `app/cmake/futari_renut_gpu.cmake:139-146` adds the source + `FUTARI_RENUT_RENDERER=1` on that source only, with
  `SKIP_PRECOMPILE_HEADERS ON`). Our `cmake/futari_renut_gpu.cmake:174-191` does the same behind
  `option(FUTARI_RENUT_CORE_HOOKS ... OFF)` (`:36`), adding the source to each `futari_<core>` target *after*
  `dll_targets.cmake` created it, so `scripts/regen.sh` does not need to learn about it (the scoping doc's §2 remark is
  superseded by this cmake).

## (c) Address / symbol match and the generator

### The map and the generated code

`src/renut_engine/futari_d3d_functions.txt` (74 lines, 70 mapped names; `:30-31` note that `D3DArrayTexture_GetArraySize`
and `D3DBaseTexture_GetLevelCount` are the same folded function `8209D080`, hooked once as GetLevelCount). A script over
`generated/default/futari_recomp.*.cpp` finds a `DEFINE_REX_FUNC(sub_<ADDR>)` for **all 70** (0 missing). The requested
five, plus the three that matter most for regeneration:

| Name | Address | `DEFINE_REX_FUNC` | Site in shipped hooks.h |
|---|---|---|---|
| D3DDevice_Swap | 820A5408 | `generated/default/futari_recomp.2.cpp:26860` | real (`:805-810`) |
| D3DDevice_DrawVerticesUP | 8209DC98 | `futari_recomp.2.cpp:8722` | real (`:440`) |
| Direct3D_CreateDevice | 820A4AC0 | `futari_recomp.2.cpp:25395` | real (`:1019-1024`) |
| D3DDevice_SetTexture | 8209D660 | `futari_recomp.2.cpp:7798` | real |
| D3D_CDevice_KickOff | 820A8FC0 | `futari_recomp.2.cpp:36242` | **empty** (`:916`, yields) |
| D3DDevice_BeginVertices | 8209D800 | `futari_recomp.2.cpp:8024` | **empty** (`:389`) |
| D3DDevice_Clear | 820A19F0 | `futari_recomp.2.cpp:17915` | **empty** (`:397`) |
| D3DDevice_Resolve | 820A2280 | `futari_recomp.2.cpp:19258` | **empty** (`:597`) |

All of them are weak by construction (`DEFINE_REX_FUNC` -> `REX_WEAK_FUNC`, `futari_init.h:65-67`, `context.h:52`).
Also: the vblank handler `sub_820A4C08` the vblank thread calls (`futari_renut_vblank.cpp:84 kVblankHandler`) is at
`futari_recomp.2.cpp:25602`; the thread first hashes 69 words of it against `0x49BA3A1B1EAA1B6E` (`:85-86, 96-108`) and
refuses to raise vblanks on a mismatch (a warning), so a first run must show no `[renut] vblank: ... differs` line.

### tools/renut/renut_futari_hooks.py (377 lines, identical to futari-linux tools/)

Inputs:
- `MAP = ENGINE/futari_d3d_functions.txt` (`:33`, parsed `:47-61`: `<name> <hex addr> <why>`, `#` comments; aborts on
  a name or address listed twice).
- The renderer sources under `GPU` (`:64-68`, every `*.cpp`/`*.h` except `banjo_renut_select.h`): rewritten in place
  from `BANJO_RENUT_HOOK*` to `FUTARI_RENUT_HOOK*` (`:71-102`, idempotent; ours are already rewritten), and scanned
  for the set of hook names (`:95-97`). A mapped name with no hook aborts (`:346-348`).
- The generated code: `--gen DIR` (default `APP/generated`, `:341-343`); reads `<gen>/default/*_recomp.*.cpp` and
  `<gen>/<core>/*_recomp.*.cpp` (`:160-180`), matching `^(?:DEFINE_REX_FUNC|PPC_FUNC_IMPL)\((?:__imp__)?sub_([0-9A-F]{8})\)`
  (`:39`) and collecting each function's `\t// <insn>` comment lines as its instruction body (`:173`). Cores are matched
  by instruction equality with branch targets ignored, else with immediates ignored, unique, and with a consistent call
  graph (`:187-229`). **No .xex, no layout file** (the device-offset translation is a separate, already-applied tool,
  `renut_futari_layout.py`).
- `APP/src` (minus `ENGINE`) scanned for Futari's own hooks of default.xex addresses (`REX_HOOK_RAW|REX_HOOK|REX_STUB(sub_82…)`
  or `extern "C" REX_FUNC(sub_82…) {`, `:109-124`) -> these get an empty "yields" site.
- `CORE_SRC = APP/src/native/futari_native_core.cpp` (`:36`), opened **unconditionally** in `futari_owned_cores()`
  (`:132`) and `check_yields()` (`:331`).

Outputs (`:34-35`, written only when changed, `:356-361`): `ENGINE/futari_renut_hooks.h` (`write_header`, `:244-274`)
and `ENGINE/futari_renut_core_hooks.cpp` (`write_core`, `:294-318`). `--check` prints what it would write and still
returns 1 on missing yields (`:321-336, 370-373`).

Why it cannot run here as-is:
1. `ROOT = dirname(dirname(abspath(__file__)))` (`:29`) assumes the script sits at `tools/<script>`; ours is
   `tools/renut/<script>`, so `ROOT` = `<repo>/tools` and `APP` = `<repo>/tools/app` (`:30`).
2. `APP/src/renut_engine` (`:31`) vs our `src/renut_engine`; `APP/generated` default vs our `generated/`.
3. `CORE_SRC` does not exist -> `FileNotFoundError` at `:132` (and `:331`).
4. Minor: `futari_owned_default()` would record `main.cpp:792`'s `extern "C" REX_FUNC(sub_821CBFF0) {` as owned; harmless
   (not in the map), but it shows the scan is live, so any future D3D-address hook in `src/` would silence a site.

Minimal patch (not applied): `ROOT = dirname(dirname(dirname(abspath(__file__))))` (or an env override), `APP = ROOT`,
`ENGINE = ROOT/'src'/'renut_engine'`, and in `futari_owned_cores()` / `check_yields()` treat a missing `CORE_SRC` as
"no owned cores" (empty sets, skip the yield check). With that, since `owned` and `owned_cores` are empty here, the
generator writes **real sites for the 7 previously yielded names** and real `FUTARI_RENUT_CORE_HOOK` lines for them
in all 8 cores; `core_names` stays the 45 names found in at least one core, so `FUTARI_RENUT_CORE_COUNT` stays 45 and
the exe/core tables keep agreeing.

Command line (Git Bash, repo root; do not run yet):

```
cd /c/Users/shant/Desktop/projects/futari_recomp
python3 tools/renut/renut_futari_hooks.py --gen generated --check
python3 tools/renut/renut_futari_hooks.py --gen generated
```

(Only stdlib imports: `glob os re sys`; files are opened as UTF-8 explicitly, so Windows Python needs no encoding
work-around. Expected summary line: `N renderer hooks, 70 mapped: 70 default.xex sites, 0 yield to Futari's own hooks`
and `45 of 70 found, 45 hooked here` per core.)

## (d) Boot surface (reference app/src/futari_app.h, futari-linux; futari-nx differs only in comments)

Includes (`futari_app.h:33-41`, under `#if FUTARI_RENUT_RENDERER`): `<rex/frame_latency.h>`,
`<rex/ui/immediate_drawer.h>`, `"renut_engine/gpu/banjo_renut_capture.h"`, `"renut_engine/gpu/banjo_renut_select.h"`,
`"renut_engine/gpu/overlay/app_integration.h"`, `"renut_engine/futari_renut_vblank.h"`.

Signatures (our copies, identical to upstream):
- `void renut::gpu::ConfigureRuntime(rex::RuntimeConfig&)` (`gpu/overlay/app_integration.h:23`; body
  `app_integration.cpp:84-87`: `config.graphics = nullptr; config.gpu_plugin.clear();`).
- `std::unique_ptr<rex::ui::ImmediateDrawer> renut::gpu::CreateOverlayDrawer()` (`:26`; `OverlayDrawer`, plume).
- `bool renut::gpu::StartRenderer(rex::ui::WindowedAppContext&, std::function<rex::ui::ImGuiDrawer*()>)` (`:32-33`;
  creates the host device, starts reNut's overlay, "routes the SDK's menus into Present, and presents overlay-only frames
  whenever the guest is not presenting", `:28-31`).
- `void renut::gpu::OnWindowResized()` (`:36`), `void renut::gpu::ShutdownRenderer()` (`:39`).
- `void futari::renut_vblank::Start() / Stop()`, `uint64_t Count()` (`futari_renut_vblank.h:10-13`).
- `bool renut::gpu::CaptureFrame(rex::ui::RawImage&, int timeout_ms = 1000)` (`gpu/banjo_renut_capture.h:13`), only used
  by futari-nx's `futari::capture` video tool (not in our tree; optional).
- `bool banjo::renut_gpu::Active()` (`gpu/banjo_renut_select.h:26`).

Order at boot, as the SDK calls the overrides (`thirdparty/rexglue-sdk/src/ui/rex_app.cpp:89-105` OnInitialize ->
SetupEnvironment -> SetupPresentation -> OnFinalizePaths -> ConstructRuntime -> LaunchModule):

| # | SDK phase (rex_app.cpp) | futari_app.h override | Renut call |
|---|---|---|---|
| 1 | SetupEnvironment: `OnConfigurePaths` `:150`, `LoadConfig` `:172`, `OnPostInitLogging` `:183` | `OnPostInitLogging` `:76-92` | none; defaults `gpu_plugin` to `"xenos"` via `rex::cvar::HasNonDefaultValue("gpu_plugin")` / `SetFlagByName("gpu_plugin","xenos")` (both in our `cvar.h:179,217`) |
| 2 | SetupPresentation: `config_.gpu_plugin = REXCVAR_GET(gpu_plugin)` `:317`, `OnPreSetup(config_)` `:322` | `OnPreSetup` `:136-149` | `if (banjo::renut_gpu::Active()) { renut::gpu::ConfigureRuntime(config); REXLOG_INFO(...); return; }` |
| 3 | SetupPresentation, detached branch `:385-393`: `immediate_drawer_ = OnCreateImmediateDrawer(); SetupOverlays(nullptr, drawer)` -> `OnCreateDialogs` `:450` | `OnCreateImmediateDrawer` `:231-234` | `if (Active()) return renut::gpu::CreateOverlayDrawer(); return rex::ReXApp::OnCreateImmediateDrawer();` |
| 4 | `OnFinalizePaths(resolved_defaults_, resume)` `:95` ("the window exists here, UI thread, before the game starts") | `OnFinalizePaths` `:244-277` | `if (Active()) renut::gpu::StartRenderer(app_context(), [this] { return imgui_drawer(); });` then the base/wizard path |
| 5 | ConstructRuntime: `OnLoadXexImage` `:271`, `OnPostLoadXexImage` `:302`, `OnPostSetup` `:311` | `OnPostSetup` `:168-209` | `if (Active()) futari::renut_vblank::Start();` (+ optional `futari::capture::Start(CaptureFrame, rex::latency::SwapCount)`) |
| 6 | LaunchModule: `OnPreLaunchModule` `:477`, `OnPostLaunchModule` `:495` | (ours: title + DLC, `main.cpp:699-704`) | none |
| run | `OnResize` -> `OnWindowPixelSizeChanged(w,h)` `:546-551` | `OnWindowPixelSizeChanged` `:235-238` | `rex::ReXApp::OnWindowPixelSizeChanged(w,h); if (Active()) renut::gpu::OnWindowResized();` |
| end | `OnDestroy` -> `OnShutdown` `:583-585` | `OnShutdown` `:216-227` | `if (Active()) { futari::renut_vblank::Stop(); renut::gpu::ShutdownRenderer(); }` (after stopping capture) |

Every virtual used exists in our SDK: `rex_app.h:101 OnPreSetup`, `:107 OnPostSetup`, `:113 OnShutdown`,
`:135 OnFinalizePaths`, `:147 OnPostInitLogging`, `:187 OnCreateImmediateDrawer`, `:198 OnWindowPixelSizeChanged`;
`RuntimeConfig{graphics, gpu_plugin, ...}` at `runtime.h:66-74`. The `rex_app.h:168-187` comment documents the detached
contract (drawer created presenter-less, `CreateTexture` must return nullptr until the device exists; the app owns
present timing / vsync / letterbox).

GPU plugin decision: both trees build the xenos plugin unconditionally (`rexglue_setup_target(futari GPU_PLUGINS xenos)`,
ours `CMakeLists.txt:54`, theirs `app/CMakeLists.txt:184`). The SDK loads it only when `config.graphics` is empty and
`gpu_plugin` non-empty (`rex_app.cpp:324-325`); `ConfigureRuntime` clears both, so with renut no plugin loads and no SDK
presenter exists. Overlay: `SetupOverlays(nullptr, drawer)` wires F3/F4/backtick onto Renut's drawer; `StartRenderer`
pumps overlay-only frames until the guest presents. Our `main.cpp` note: `OnPreSetup` (`:675-692`) sets
`config.gpu_plugin = "xenos"` when empty, so the Renut branch must come **after** that (or before and `return`), and
`present_vsync` (`main.cpp:205,290-292,428-435`) is the xenos presenter's knob: under Renut it is inert and `renut_vsync`
(`present.cpp:67`) is the one to surface. `futari_renderer` is `kInitOnly`: it comes from `futari.ini`'s managed cvar
block / command line, not the F4 menu.

## (e) cvar collisions and the shipped Linux values

Scan (`REXCVAR_DEFINE_*\(\s*name`, comments excluded) over `src/renut_engine/**`, `src/main.cpp`,
`thirdparty/rexglue-sdk/**`:

- Renut: **142** cvars in 24 files (`banjo_renut_tuning.cpp` 72, `futari_renut_shim.cpp` 16, `render/draw.cpp` 13,
  `backend/queued.cpp` 7, `backend/present.cpp` 6, `display_sync.cpp` 4, `backend/render_thread.cpp` 4, `backend/backend.cpp`
  3, `render/shader_library.cpp` 3, `futari_renut_vblank.cpp` 2, `banjo_renut_record.cpp` 2, `render/pipeline.cpp` 2, one each
  in `banjo_renut_select.cpp` (`futari_renderer`), `d3d/hooks/device.cpp`, `guest/callbacks.cpp`, `guest/command_buffers.cpp`,
  `backend/gpu_profile.cpp`, `backend/memory_report.cpp`, `render/graphics_options.cpp`, `render/surfaces.cpp`).
- `src/main.cpp`: **0** definitions (it only sets SDK cvars by name: `present_vsync`, `license_mask`, `mnk_mode`, ...).
- SDK: 224.
- **Intersections: renut x SDK = {}, renut x main = {}, main x SDK = {}; no name defined twice inside renut_engine.**
- Externs Renut declares (`REXCVAR_DECLARE`, 24): 18 are defined inside renut_engine (the 16 of
  `futari_renut_shim.cpp`, `banjo_renut_diag_gpu_shaders` in tuning.cpp, `banjo_renut_neon_compare` in queued.cpp);
  **6 are futari-nx's**: `futari_vblank_sleep_exact`, `futari_vblank_display_sync`, `futari_vblank_delay_us`,
  `futari_vblank_phase_auto`, `futari_vblank_phase_max_us` (`futari_renut_vblank.cpp:58-62`, `present.cpp:44`; upstream
  `app/src/me/me_native_system.cpp:87-125`) and `futari_late_input` (`render_thread.cpp:57`; upstream
  `app/src/native/futari_native_frame.cpp:42`). `src/renut_shim/renut_shim_stubs.cpp:18-53` defines all six with the
  upstream defaults (true, true, 0, true, 9000, true), **but `renut_shim_stubs.cpp` is not listed in
  `cmake/futari_renut_gpu.cmake`'s `add_library(futari_renut_gpu OBJECT ...)` (`:142-150`)**: add it or the link fails on
  those six (and on `RexSwitchPerfCount` / `RexSwitchPerfHitch`, `:61-62`).
- SDK behaviour if a name were doubled: `cvar.cpp:192` `REXLOG_ERROR("cvar: duplicate registration of '{}'; second
  registration ignored")` (no abort); unknown config keys `cvar.cpp:650` `REXLOG_WARN("Config: unknown cvar '{}'")`.

`futari-linux/app/futari_linux.toml` (the shipped configuration, 7 keys):

| Key | Value | Default | Non-default? |
|---|---|---|---|
| `futari_renderer` | `"renut"` | `"default"` (`banjo_renut_select.cpp:15`) | yes: the switch |
| `renut_gpu_backend` | `"vulkan"` | `"d3d12"` (`backend.cpp:28-33`, allowed d3d12/vulkan/deko3d) | yes: Vulkan-only first cut per scoping §7 |
| `futari_renderer_native` | `true` | futari-nx `me/` cvar, not in our tree | n/a (would log "unknown cvar"; Renut returns before `me::native` anyway) |
| `fullscreen` | `true` | SDK | yes (SDK key) |
| `futari_fill_crop` | `false` | `false` (`present.cpp:74`) | no |
| `renut_vsync` | `false` | `false` (`present.cpp:67`) | no (least input lag, tearing possible; `true` waits for refresh) |
| `log_level` | `"warn"` | SDK | yes (SDK key) |

So for us: `futari_renderer = renut`, `renut_gpu_backend = vulkan`; `renut_vsync` is the user's call (it is the Renut
analogue of our `present_vsync`/VRR story). `renut_resolution` default `"720p"` (`surfaces.cpp:24`), `renut_spirv_package`
default `"vulkan_spirv.bin"` beside the exe (`shader_library.cpp:35`), `banjo_late_input` default true
(`tuning.cpp:248`) but gated off by the shim's `GroupOn -> false` (see (f)).

## (f) Other futari-nx / Switch / Linux assumptions in src/renut_engine

Non-SDK, non-renut_engine, non-std includes (full inventory): `cpu/banjo_enhancements.h` (3), `me/me_platform_compat.h`
(4), `native/futari_native.h` (2), `profiler/profiler.h` (1), `me/masseffect/banjo_pmull_fingerprint.h` (1): the five,
all present in `src/renut_shim/` (untracked, with `renut_shim_stubs.cpp`). Beyond them:

1. **`futari_enhancements.h`** (`futari_renut_shim.cpp:10`), a 6th header: `banjo::EnhancementActive` (`:76-80`) =
   own key AND `futari::enhancements::GroupOn(group)`. The shim `src/renut_shim/futari_enhancements.h:20,27` returns
   `kGroupOn = false`, so **every `banjo_*` enhancement is off** regardless of its own cvar (incl. `banjo_late_input`,
   `banjo_renut_pipeline_cache`, ...). Deliberate ("Renut on its plain paths"), worth a line in the port notes.
2. **`renut_logging.h`** includes `"generated/renut_init.h"` (`:4`), N&B's codegen header: **nobody includes
   `renut_logging.h`** (grep over renut_engine: 0), so it is dead; delete it rather than shim the header.
3. **`native/futari_native.h` symbols**: `futari::native::CodeHash` (`futari_renut_vblank.cpp:99`, the handler check) and
   `CodegenChecksReport` (`futari_renut_shim.cpp:83`): shimmed (`src/renut_shim/native/futari_native.h:31,43`).
4. **Six external cvars** (see (e)): shimmed in `renut_shim_stubs.cpp`, not yet compiled.
5. **Switch SDK `extern "C"` symbols**: `RexSetGuestThreadCoreLayout` (`device.cpp:62`; the call at `:884` is under
   `#if REX_PLATFORM_SWITCH`, `:882`), `RexSwitchPerfCount` (`perf/frame_stats.cpp:13`, called unguarded at `:102`;
   stubbed `renut_shim_stubs.cpp:61`), `RexStoreCardsHostBytes` (`draw.cpp:905`; defined `futari_renut_shim.cpp:72`),
   `g_banjo_store_cards` (`futari_renut_shim.cpp:70`). Not in our SDK (grep: none), so the stubs are required.
6. **`futari_app.h`, `futari_native_core`, census, `futari_api`**: none. `census` appears only in comments
   (`tuning.cpp:511`, `tuning.h:220`, `draw.cpp:105,4904`). `futari_renut_core.h:4` and `core_hooks.cpp:4-5` mention
   `futari_native_core.cpp` in comments only. Our `cmake/futari_renut_gpu.cmake:19` already refuses a census build
   together with Renut.
7. **Switch / deko3d**: compile-time code is guarded: `PLUME_DEKO3D` / `REX_PLATFORM_SWITCH` / `__SWITCH__` in
   `native_window.cpp:8-19,73,150`, `futari_renut_vblank.cpp:42,54,87,123`, `display_sync.cpp` (5 guards),
   `banjo_renut_select.cpp:10-13,31-40`, `backend.cpp` (8), `video.cpp` (13), `draw.cpp` (12), `tuning.cpp` (18),
   `memory_report.cpp:16`, `present.cpp` (6), `shader_library.cpp` (3), `overlay_drawer.cpp` / `overlay_ui.cpp` (the
   `host_shaders/*.deko3d.h` includes). The unguarded mentions in `pipeline.cpp`, `surfaces.cpp:248,493,576`,
   `textures.cpp:1073`, `format.cpp:48` are run-time `IsDeko3D()` branches (`backend.h:33`, `enum class Backend
   { D3D12, Vulkan, Deko3D }` `:19`) or cvar help text; `banjo_renut_deko3d.cpp` is compiled only on Switch upstream
   (`app/cmake/futari_renut_gpu.cmake:117-119`) and is not in our source list. `renut_gpu_backend` still accepts
   `"deko3d"` (`backend.cpp:32`; "elsewhere it means vulkan").
8. **Linux-only**: the Wayland/X11 Vulkan surface and `SDL_APP_ID` block in `native_window.cpp:95-146,150-251` are under
   `#else` of `_WIN32`/`__SWITCH__` and `#if !defined(_WIN32) && !defined(__SWITCH__)`; `PLUME_SDL_VULKAN_ENABLED` blocks
   (`backend.cpp:47,302`) are only set by `gpu.cmake:50` on Linux; `memory_report.cpp:62 #elif defined(__linux__)`. The
   Windows branch of `native_window.cpp:23-72` finds the SDL window by class name `L"SDL_app"` (`:37`).
9. **SDL3 off-Switch**: `futari_renut_vblank.cpp:42-43 #if !REX_PLATFORM_SWITCH #include <SDL3/SDL_video.h>` and
   `DisplayRefreshNs()` `:246-254` (`SDL_GetWindows`, `SDL_GetDisplayForWindow`, `SDL_GetCurrentDisplayMode`): compiled on
   Windows too. Fine: the SDK's Windows window is SDL3 (`thirdparty/rexglue-sdk/src/ui/window_sdl.cpp`,
   `windowed_app_context_sdl.cpp`), but the `futari_renut_gpu` target must see SDL3's include dir (upstream gets it the
   same way; check the first configure).
10. **Windows-only includes** are guarded: `memory_report.cpp:12-14 <psapi.h>`, `video.cpp:21-23 <io.h> <share.h>`,
    `pipeline.cpp:36-37 <io.h>` under `_WIN32`; `<malloc.h>` under `__SWITCH__` (`memory_report.cpp:16-17`);
    `<arm_neon.h>` under `__aarch64__` (`line_compare.h:10-11`, `vertex_format.cpp:14-15`; relevant for the macOS arm64
    leg, not Windows x64).
11. **Generated headers the renderer expects**: `host_shaders/*.{spirv,dxil,deko3d}.h`, `shader_cache.h`,
    `shader_common.h` (XenosRecomp's), produced by `gpu/gpu.cmake` (`:96-200`; our edits add `RENUT_TOOLS_DIR`,
    `RENUT_IMGUI_DIR` -> the SDK's `thirdparty/rexglue-sdk/thirdparty/imgui`, and the stub-cache guard at `:32`).
    `cmake/renut_shader_cache_stub.cpp.in` + `FUTARI_RENUT_STUB_SHADER_CACHE` (ours `:33`) give an empty table for the
    compile leg.
12. **The generated hook files themselves** are futari-linux's output (7 yields each, see (a)-(c)): regenerate before the
    first renut boot.

## Tree state observed (for the record)

Branch `renderer/renut` at `99a80a3` (leg 1 committed). Untracked: `src/renut_engine/`, `src/renut_shim/`, `cmake/`,
`thirdparty/plume/`, `thirdparty/reblue-XenosRecomp/`, `tools/`; modified: `CMakeLists.txt` (Renut option + include,
`:58-64`), `NOTICE.md`, `THIRD-PARTY-NOTICES.txt`, `scripts/gen_third_party_notices.sh`, `rexglue-sdk/include/rex/platform.h`.
Nothing in this analysis was changed; this file is the only addition.
