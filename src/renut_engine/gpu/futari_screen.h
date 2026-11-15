// futari_recomp (2026-11-15): Futari's gameplay image in Renut (the game layer, as render/graphics_options.cpp).
//
// The game draws gameplay into targets of its own size (futari_screen_rules.h), resolves them into textures and
// draws one of those into the frame (the blit). These targets stay at 1x under every renut_resolution, so the blit
// is the only resample; with Sharp bilinear or On, the blit's pixel shader is a host one (sharp_bilinear_ps, or
// blit_bilinear_ps for float bilinear).
#pragma once

#include <cstdint>
#include <optional>

#include <rex/types.h>

#include "renut_engine/gpu/d3d/d3d.h"

namespace renut::gpu {

// The surface 'desc' describes is one the game draws gameplay into.
bool IsGameplayDescriptor(const d3d::SurfaceDesc &desc);

// A colour resolve from the D3DSurface at 'source_va' into the texture whose header's fetch constant is 'dest_fetch'
// (CPU address, as the resolve gets it): its data page holds the gameplay image now when the source is a gameplay
// target, and no longer when it is another.
void NoteResolveLocked(u32 source_va, const d3d::TextureFetchConstant &dest_fetch);

// The draw's host pixel shader (PipelineKey::host_pixel_shader, futari_screen_rules.h): for the gameplay blit
// sharp_bilinear_ps while Sharp bilinear is on, blit_bilinear_ps while On is, else (or without that shader) the
// guest's. 'ps_hash' is the draw's pixel shader (0 without one), 'slot0' the
// device's fetch constant 0, 'colour0_va' its render target 0 (0 without one).
// It also notes, under every Smoothing mode, the page of the gameplay resolve the blit samples.
u32 BlitHostPixelShaderLocked(uint64_t ps_hash, const d3d::TextureFetchConstant &slot0, u32 colour0_va);

// The base page of the gameplay resolve the last blit since the previous call sampled, or nothing when no blit was
// drawn since. The present calls it once a frame (backend/present.cpp, the dev capture of that image).
std::optional<u32> TakeBlitSourcePageLocked();

// The game's Smoothing word (0 Off, 1 On, 4 Sharp bilinear; also declared in banjo_renut_select.h for the app). Any
// thread; the render thread takes it at its next draw.
void SetBlitSmoothing(u32 game_mode);

}  // namespace renut::gpu
