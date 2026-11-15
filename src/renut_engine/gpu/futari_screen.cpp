// futari_recomp (2026-11-15): see futari_screen.h; also defines the blit seam render/graphics_options.h declares.
#include "renut_engine/gpu/futari_screen.h"

#include <atomic>

#include "renut_engine/gpu/backend/video.h"
#include "renut_engine/gpu/futari_screen_rules.h"
#include "renut_engine/gpu/guest/guest_memory.h"
#include "renut_engine/gpu/log.h"
#include "renut_engine/gpu/render/graphics_options.h"
#include "renut_engine/gpu/render/texture_layout.h"

namespace renut::gpu {

namespace {
std::atomic<futari_screen::BlitSmoothing> g_blit_smoothing{futari_screen::BlitSmoothing::kGame};
futari_screen::GameplayResolvePages g_gameplay_pages;  // State().mutex
futari_screen::LastBlitSource g_blit_source;          // State().mutex

const d3d::SurfaceDesc *SurfaceDescAt(u32 surface_va) {
  return surface_va ? GuestPtr<const d3d::SurfaceDesc>(surface_va + sizeof(d3d::Resource)) : nullptr;
}
}  // namespace

bool IsGameplayDescriptor(const d3d::SurfaceDesc &desc) {
  return futari_screen::IsGameplayTarget(d3d::SurfaceWidth(desc.Size), d3d::SurfaceHeight(desc.Size), desc.Format,
                                         u32(desc.ColorInfo) & 0xFFF);
}

void NoteResolveLocked(u32 source_va, const d3d::TextureFetchConstant &dest_fetch) {
  const d3d::SurfaceDesc *source = SurfaceDescAt(source_va);
  // A header's fetch constant holds the CPU address; the page is the GPU one, as the blit's slot 0 gives it.
  g_gameplay_pages.Note(source && IsGameplayDescriptor(*source), TextureFetch::FromHeader(dest_fetch).BasePage());
}

u32 BlitHostPixelShaderLocked(uint64_t ps_hash, const d3d::TextureFetchConstant &slot0, u32 colour0_va) {
  // Any other pixel shader (all but the blit's few draws a frame) costs this compare.
  if (ps_hash != futari_screen::kBlitPixelShaderHash)
    return 0;
  const u32 page = TextureFetch::FromGuest(slot0).BasePage();
  const d3d::SurfaceDesc *colour0 = SurfaceDescAt(colour0_va);
  const bool gameplay_source = g_gameplay_pages.Contains(page);
  const bool gameplay_target = colour0 && IsGameplayDescriptor(*colour0);
  // Under every Smoothing mode, for the dev capture of the image the blit sampled.
  if (futari_screen::IsGameplayBlit(ps_hash, gameplay_source, gameplay_target))
    g_blit_source.Note(page);
  const u32 shader = futari_screen::BlitHostPixelShader(g_blit_smoothing.load(std::memory_order_relaxed), ps_hash,
                                                        gameplay_source, gameplay_target);
  // No shader (failed to build, warned once in video.cpp; deko3d): the game's own, i.e. On.
  if ((shader == kHostPixelShaderSharp && !State().sharp_bilinear_ps) ||
      (shader == kHostPixelShaderBilinear && !State().blit_bilinear_ps))
    return kHostPixelShaderGuest;
  return shader;
}

std::optional<u32> TakeBlitSourcePageLocked() {
  return g_blit_source.Take();
}

void SetBlitSmoothing(u32 game_mode) {
  using futari_screen::BlitSmoothing;
  const BlitSmoothing smoothing = futari_screen::BlitSmoothingOf(game_mode);
  if (g_blit_smoothing.exchange(smoothing, std::memory_order_relaxed) != smoothing)
    RGPU_INFO("Smoothing: {}", smoothing == BlitSmoothing::kSharp      ? "Sharp bilinear"
                               : smoothing == BlitSmoothing::kBilinear ? "float bilinear (On)"
                                                                       : "the game's");
}

}  // namespace renut::gpu
