// banjo-nx (2026-10-16): two renderers in one NRO; futari-nx (2026-10-19): Futari's renderer choice `futari_renderer`:
//   "default": Futari's own (futari_renderer_native: the native PM4-ring renderer of src/me, or the emulated Xenos);
//   "renut":   Renut's Direct3D-level renderer (github.com/masterspike52/reNut by reNut and masterspike52, banjo-nx's
//              copy in app/src/renut_engine/gpu), which runs Futari's Direct3D natively through plume: no PM4 ring and
//              no EDRAM model.
// Every hook is always linked; each runs Renut's side only when Renut's renderer is chosen, and the game's original
// otherwise. The hooks name Futari's Direct3D functions (app/src/renut_engine/futari_d3d_functions.txt), not
// addresses: tools/renut_futari_hooks.py writes each name's site in default.xex (futari_renut_hooks.h) and in the
// eight game cores (futari_renut_core_hooks.cpp). Where one of Futari's own hooks owns the address, that hook hands
// over first thing (FUTARI_RENUT_YIELD).
#pragma once

#include <cstdint>
#include <string>

#include <rex/hook.h>
#include <rex/ppc.h>

namespace banjo::renut_gpu {

// futari_renderer == "renut", read once (the choice holds for the session). Inline, a byte load after the first call
// (every Direct3D hook asks).
namespace detail {
extern int8_t g_active;  // 1 renut, -1 not, 0 not read yet
bool LatchActive();
}  // namespace detail
inline bool Active() {
  const int8_t a = detail::g_active;
  return a ? a > 0 : detail::LatchActive();
}

// futari_recomp (2026-11-07): the boot's veto, before the first Active(): the default renderer runs this session
// although futari_renderer = renut (vulkan_spirv.bin missing or from another build, where Renut would draw a black
// game). `why` is kept for the settings screen. Ignored, with an error logged, once the choice is latched.
void FallBackToDefault(const std::string& why);
// The veto's reason; empty when Renut's choice stands (or was never asked for).
const std::string& FallbackReason();

}  // namespace banjo::renut_gpu

namespace renut::gpu {
// render/shader_library.h's, declared here too for the app (which has no plume include path): true on a build
// with the translated game shaders built in, false on a distribution build that reads vulkan_spirv.bin.
bool GameShadersBuiltIn();
// futari_recomp (2026-11-07): render/surfaces.h's, declared here too for the app: renut_resolution = output.
bool ResolutionIsOutput();
// futari_recomp (2026-11-10): render/surfaces.h's, declared here too for the app.
bool ResolutionRequestsOutput();
void SetResolutionLatchGate(bool (*ready)(bool output, uint32_t scale_code));
// futari_recomp (2026-11-10), d3d/hooks/device.cpp: 'hook' runs on the guest thread at the start of every Swap,
// before the frame goes to the render thread (nullptr: none). Callable from any thread.
void SetGuestSwapHook(void (*hook)(uint32_t device));
// Resizes the device's automatic back and front buffers in place to width x height (the objects and their addresses
// stay; the render thread is drained first), with its stored present parameters. Guest thread, from the swap hook.
// True when the buffers are that size now; false when the front buffer's larger block could not be allocated (then
// nothing changed, one warning).
bool ResizeAutoBuffers(uint32_t device, uint32_t width, uint32_t height);
}  // namespace renut::gpu

namespace banjo::renut_gpu {

}  // namespace banjo::renut_gpu

#include "renut_engine/futari_renut_hooks.h"

// Renut's hook of the Direct3D function `name`: its entry (for the default.xex site, a yielding Futari hook and the
// cores' forwarding), then the site.
#define FUTARI_RENUT_HOOK(name, function)                                              \
  extern "C" void futari_renut_entry_##name(PPCContext& ctx, uint8_t* base) {          \
    ::rex::ppc::HostToGuestFunction<function>(ctx, base);                              \
  }                                                                                    \
  FUTARI_RENUT_SITE_##name

// Renut's raw hooks (the body follows the macro, as after REX_HOOK_RAW).
#define FUTARI_RENUT_HOOK_RAW(name)                                                    \
  static void futari_renut_raw_##name(PPCContext& ctx, uint8_t* base);                 \
  extern "C" void futari_renut_entry_##name(PPCContext& ctx, uint8_t* base) {          \
    futari_renut_raw_##name(ctx, base);                                                \
  }                                                                                    \
  FUTARI_RENUT_SITE_##name                                                             \
  static void futari_renut_raw_##name([[maybe_unused]] PPCContext& __restrict ctx, uint8_t* base)
