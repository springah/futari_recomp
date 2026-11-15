// futari_recomp (2026-11-15): PipelineKey::host_pixel_shader's values (render/pipeline.h): the host pixel shader that
// replaces the guest's in a draw, chosen per draw by a game layer (BlitHostPixelShaderLocked, graphics_options.h).
// Pure std, so a game layer's rules and their test can use it.
#pragma once

#include <cstdint>
#include <string>

namespace renut::gpu {

// The guest's, sharp_bilinear_ps, blit_bilinear_ps (the same with k = 1).
constexpr uint32_t kHostPixelShaderGuest = 0;
constexpr uint32_t kHostPixelShaderSharp = 1;
constexpr uint32_t kHostPixelShaderBilinear = 2;

// A host pixel shader's suffix in a pipeline's debug name (render/pipeline.cpp): none for the guest's, else its name
// (a value with no shader of its own, as from a corrupt pipeline list: " host_ps_<n>").
inline std::string HostPixelShaderName(uint32_t host_pixel_shader) {
  switch (host_pixel_shader) {
  case kHostPixelShaderGuest:
    return {};
  case kHostPixelShaderSharp:
    return " sharp_bilinear_ps";
  case kHostPixelShaderBilinear:
    return " blit_bilinear_ps";
  default:
    return " host_ps_" + std::to_string(host_pixel_shader);
  }
}

}  // namespace renut::gpu
