// futari_recomp (2026-11-15): futari_screen's rules (futari_screen.h), pure std, so tests/futari_screen_test.cpp
// checks them on their own.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "renut_engine/gpu/render/host_pixel_shader.h"

namespace renut::gpu::futari_screen {

// The gameplay blit's pixel shader (content hash): rgb = texture (fetch slot 0) x c2.rgb, alpha = c2.w. The game
// binds it for both Smoothing modes it knows (Off and On); only the sampler's filter differs.
constexpr uint64_t kBlitPixelShaderHash = 0xB504A970B5672C5Full;

// The targets the game draws gameplay into, at its own size (D3DFORMAT word, EDRAM base = ColorInfo bits 0-11):
// Arcade 320x240 and Xbox 360 640x480, format 1A2201BF at base 1808; Xbox 360 640x512, format 28280186 at any base
// (it follows the boot back buffer, unknown when that took the over-EDRAM path). An 80x60 downsample target shares
// the first two's format and base; its size tells it apart.
constexpr bool IsGameplayTarget(uint32_t width, uint32_t height, uint32_t format, uint32_t base) {
  if (width == 640 && height == 512)
    return format == 0x28280186u;
  const bool size = (width == 320 && height == 240) || (width == 640 && height == 480);
  return size && format == 0x1A2201BFu && base == 1808;
}

// How many gameplay resolve destinations are kept; past it the oldest goes.
constexpr std::size_t kGameplayResolvePages = 16;

// The base pages of the textures a gameplay target was resolved into last (the game resolves each frame into a new
// page and the blit samples that one). A resolve from another target into a page takes it out.
class GameplayResolvePages {
 public:
  void Note(bool source_is_gameplay, uint32_t page) {
    const auto it = std::find(pages_.begin(), pages_.end(), page);
    if (it != pages_.end())
      pages_.erase(it);
    if (!source_is_gameplay)
      return;
    if (pages_.size() == kGameplayResolvePages)
      pages_.erase(pages_.begin());
    pages_.push_back(page);
  }
  bool Contains(uint32_t page) const { return std::find(pages_.begin(), pages_.end(), page) != pages_.end(); }

 private:
  std::vector<uint32_t> pages_;  // oldest first
};

// The draw is the gameplay blit, under every Smoothing mode: the pixel shader is the blit's, fetch slot 0 samples a
// gameplay resolve and colour target 0 is not a gameplay target (the frame, at any size).
constexpr bool IsGameplayBlit(uint64_t ps_hash, bool slot0_is_gameplay_resolve, bool colour0_is_gameplay) {
  return ps_hash == kBlitPixelShaderHash && slot0_is_gameplay_resolve && !colour0_is_gameplay;
}

// How the gameplay blit is filtered: the game's own shader (Off, and any mode but On and Sharp), float bilinear (On;
// the sampler's own filter weights are coarser) or Sharp bilinear.
enum class BlitSmoothing : uint32_t { kGame, kBilinear, kSharp };

// From the game's Smoothing word (the display block's +68): 0 Off, 1 On, 4 Sharp bilinear.
constexpr BlitSmoothing BlitSmoothingOf(uint32_t game_mode) {
  if (game_mode == 4)
    return BlitSmoothing::kSharp;
  return game_mode == 1 ? BlitSmoothing::kBilinear : BlitSmoothing::kGame;
}

// The draw's host pixel shader (render/host_pixel_shader.h's values): the one 'smoothing' asks for when the draw is the gameplay blit, else the guest's.
constexpr uint32_t BlitHostPixelShader(BlitSmoothing smoothing, uint64_t ps_hash, bool slot0_is_gameplay_resolve,
                                       bool colour0_is_gameplay) {
  if (smoothing == BlitSmoothing::kGame || !IsGameplayBlit(ps_hash, slot0_is_gameplay_resolve, colour0_is_gameplay))
    return kHostPixelShaderGuest;
  return smoothing == BlitSmoothing::kSharp ? kHostPixelShaderSharp : kHostPixelShaderBilinear;
}

// What fetch slot 0 of a gameplay blit sampled: the base page and the fetch constant's swizzle (which may differ from
// the one the resolve destination's own view was made with).
struct BlitSource {
  uint32_t page = 0;
  uint32_t swizzle = 0;
  friend bool operator==(const BlitSource &, const BlitSource &) = default;
};

// The frame's last gameplay blit's source, for the dev capture of that image: noted per blit, taken (and so
// forgotten) once per present.
class LastBlitSource {
 public:
  void Note(uint32_t page, uint32_t swizzle) { source_ = BlitSource{page, swizzle}; }
  std::optional<BlitSource> Take() { return std::exchange(source_, std::nullopt); }

 private:
  std::optional<BlitSource> source_;
};

}  // namespace renut::gpu::futari_screen
