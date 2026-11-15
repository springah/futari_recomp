/**
 * @file    gpu/render/graphics_options.cpp
 *
 * futari-nx (2026-10-19): Futari's version. N&B's per-target choices (its
 * shadow maps, SSAO and post-processing buffers, recognised by their sizes and
 * EDRAM bases) are not Futari's: every target renders at the render
 * resolution in the host format of its own format, and the one option kept is
 * the anisotropy floor.
 */
#include "renut_engine/gpu/render/graphics_options.h"

#include <array>  // futari_recomp (2026-11-15)
#include <cstdlib>
#include <set>  // futari_recomp (2026-11-15)
#include <string>

#include <rex/cvar.h>
#include <rex/types.h>

// futari_recomp (2026-11-08): no guest/widescreen.h (removed with GuestScreenWidth).
#include "renut_engine/gpu/futari_screen.h"  // futari_recomp (2026-11-15): the gameplay targets
#include "renut_engine/gpu/log.h"            // futari_recomp (2026-11-15)
#include "renut_engine/gpu/render/format.h"

REXCVAR_DEFINE_STRING(renut_anisotropic_filtering, "Game", "Futari/Renut",
                      "Anisotropic texture filtering: the game's own, or at least 2x, 4x, 8x or "
                      "16x.")
    .allowed({"Game", "2x", "4x", "8x", "16x"});

namespace renut::gpu {

namespace {
uint32_t g_min_anisotropy = 1;
std::set<std::array<u32, 3>> g_logged_gameplay_targets;  // futari_recomp (2026-11-15): size, format, base
}  // namespace

void LatchGraphicsOptionsLocked() {
  const int anisotropy = std::atoi(REXCVAR_GET(renut_anisotropic_filtering).c_str());
  g_min_anisotropy = anisotropy >= 2 ? uint32_t(anisotropy) : 1;
}

// futari_recomp (2026-11-15): the gameplay targets at 1x under every render resolution, so the blit into the frame
// is the gameplay image's only resample (futari_screen.h). Each one logged once.
RenderScale SurfaceRenderScale(const d3d::SurfaceDesc &desc, RenderScale resolution) {
  if (!IsGameplayDescriptor(desc))
    return resolution;
  const u32 base = u32(desc.ColorInfo) & 0xFFF;
  if (g_logged_gameplay_targets.insert({u32(desc.Size), u32(desc.Format), base}).second)
    RGPU_INFO("Gameplay target {}x{} at 1x (base {})", d3d::SurfaceWidth(desc.Size), d3d::SurfaceHeight(desc.Size),
              base);
  return RenderScale{};
}

plume::RenderFormat SurfaceColorFormat(const d3d::SurfaceDesc &desc) {
  return ColorTargetFormat(GpuColorFormat((u32(desc.ColorInfo) >> 16) & 0xF));
}

uint32_t MinAnisotropy() {
  return g_min_anisotropy;
}

}  // namespace renut::gpu
