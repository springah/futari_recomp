// futari_recomp (2026-11-15): Futari's gameplay image in Renut (the game layer, as render/graphics_options.cpp).
//
// The game draws gameplay into targets of its own size (futari_screen_rules.h), resolves them into textures and
// draws one of those into the frame (the blit). These targets stay at 1x under every renut_resolution, so the blit
// is the only resample; with Sharp bilinear or On, the blit's pixel shader is a host one (sharp_bilinear_ps, or
// blit_bilinear_ps for float bilinear).
#pragma once

#include <cstdint>

#include <rex/types.h>

#include "renut_engine/gpu/d3d/d3d.h"

namespace renut::gpu {

// The surface 'desc' describes is one the game draws gameplay into.
bool IsGameplayDescriptor(const d3d::SurfaceDesc &desc);

// futari_screen.cpp also defines NoteResolveLocked, BlitHostPixelShaderLocked and TakeBlitSourcePageLocked, the
// seam the generic draw and present call (render/graphics_options.h): for the gameplay blit sharp_bilinear_ps while
// Sharp bilinear is on, blit_bilinear_ps while On is, else (or without that shader) the guest's.

// The game's Smoothing word (0 Off, 1 On, 4 Sharp bilinear; also declared in banjo_renut_select.h for the app). Any
// thread; the render thread takes it at its next draw.
void SetBlitSmoothing(u32 game_mode);

}  // namespace renut::gpu
