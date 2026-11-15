/**
 * @file    gpu/render/graphics_options.h
 * @brief   Graphics quality beyond the console's (Nuts&Bolts/Graphics):
 *          shadow map and ambient occlusion resolution, anisotropic
 *          filtering.
 *
 * - renut_shadow_resolution: the shadow maps' render scale. The sun's two
 *   cascades are 1024x1024 and 768x768 D24S8 targets (512x512 in split
 *   screen; sub_822998C8), and the local lights render 512x512 D24S8 depth
 *   converted in a 512x512 16_16_FLOAT target into a 2048x2048 atlas
 *   (sub_823B3CE0); by default they follow the render resolution.
 * - renut_ambient_occlusion: the SSAO pass (sub_8235FA90) renders at half
 *   the screen, into 640x360 targets at EDRAM base 1864; Full renders it at
 *   the screen's resolution.
 * - renut_anisotropic_filtering: the least a texture's linear minification
 *   filters anisotropically.
 * - renut_post_process_precision: the 8_8_8_8 targets of half the screen or
 *   less (bloom, depth of field, SSAO) as 16-bit unorm on the host. The bloom
 *   (bright pass at 640x360 from the 8-bit scene, then 320x180 down to 80x45,
 *   added at weights 1, 1.2, 1.6 and 2) carries a faint glow in 1/255 steps;
 *   upscaled to the screen they show as blobs that slide over dark areas as
 *   the camera moves. A resolve's destination takes its source's host format,
 *   so the passes reading them see the finer values too.
 */
#pragma once

#include <cstdint>
#include <optional>  // futari_recomp (2026-11-15): TakeBlitSourcePageLocked

#include "renut_engine/gpu/d3d/d3d.h"
#include "renut_engine/gpu/render/format.h"
#include "renut_engine/gpu/render/render_scale.h"

namespace renut::gpu {

// Reads the options; the rest use what it read. Once a frame, with the render
// resolution (LatchResolutionScaleLocked), so a frame never mixes them.
void LatchGraphicsOptionsLocked();

// The render scale of the target 'desc' describes, given the render
// resolution's.
RenderScale SurfaceRenderScale(const d3d::SurfaceDesc &desc, RenderScale resolution);

// The host format of the color target 'desc' describes.
plume::RenderFormat SurfaceColorFormat(const d3d::SurfaceDesc &desc);

// The anisotropy linear minification gets at least (1 = the game's own).
uint32_t MinAnisotropy();

// futari_recomp (2026-11-15): a game layer's blit policy, defined by it (Futari's: gpu/futari_screen.cpp).
// A colour resolve from the D3DSurface at 'source_va' into the texture whose header's fetch constant is 'dest_fetch'
// (CPU address, as the resolve gets it), so the game layer knows which destination pages hold its image.
void NoteResolveLocked(uint32_t source_va, const d3d::TextureFetchConstant &dest_fetch);
// The draw's host pixel shader (PipelineKey::host_pixel_shader, render/host_pixel_shader.h). 'ps_hash' is the draw's
// pixel shader (0 without one), 'slot0' the device's fetch constant 0, 'colour0_va' its render target 0 (0 without
// one). Once per draw, chain-reused ones included. For the gameplay blit: kHostPixelShaderSharp (sharp_bilinear_ps)
// while Smoothing is Sharp bilinear, kHostPixelShaderBilinear (blit_bilinear_ps) while On, else (or without that
// shader) kHostPixelShaderGuest. It also notes, under every Smoothing mode, the page of the gameplay resolve the blit
// sampled, which TakeBlitSourcePageLocked returns.
uint32_t BlitHostPixelShaderLocked(uint64_t ps_hash, const d3d::TextureFetchConstant &slot0, uint32_t colour0_va);
// The base page of the gameplay resolve the last blit since the previous call sampled (noted by
// BlitHostPixelShaderLocked, so only meaningful after the blit's call this frame), or nothing when no blit was drawn
// since. The present calls it once a frame (backend/present.cpp, the dev capture of that image).
std::optional<uint32_t> TakeBlitSourcePageLocked();

} // namespace renut::gpu
