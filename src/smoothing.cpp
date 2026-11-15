// The game's Smoothing with a third value, Sharp bilinear (4). The gameplay blit sub_82183C68 reads the mode once
// and skips its filter code for any value above 3, so for Sharp it runs as On (the linear filter and the On shader
// pair, the same draw on every renderer); on Renut the renderer is told the mode first and redraws that blit sharp
// (and On's with float bilinear, finer than the sampler's weights), on the default renderer it stays On. Dev aids for the capture runs: futari_dev_smoothing forces a mode without touching
// the save, and the blit's quad is logged once per change.

#include "generated/default/futari_init.h"

#include "smoothing.h"
#include "smoothing_math.h"

#include <array>
#include <atomic>
#include <bit>
#include <optional>
#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>

#if FUTARI_RENUT_RENDERER
#include "renut_engine/gpu/banjo_renut_select.h"
#endif

// Dev aid: draw gameplay with a chosen Smoothing (0 Off, 1 On, 4 Sharp bilinear) without touching the save, for
// captures; also logs the gameplay blit's quad:
//   --futari_dev_smoothing=4
REXCVAR_DEFINE_STRING(futari_dev_smoothing, "", "Futari",
                      "Dev: the gameplay Smoothing, 0 Off, 1 On or 4 Sharp bilinear (empty: the save's)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {

using futari::smoothing::BlitDrawMode;
using futari::smoothing::kDisplayBlockPointer;
using futari::smoothing::ParseDevSmoothing;

// The display block's Smoothing word.
constexpr uint32_t kDisplaySmoothing = 68;
// The blit draws its argument's quad with sub_8206BF48(prim, decl, 4, vertices): 4 vertices of 16 bytes, big-endian
// f32 x, y, u, v (built by sub_821852C8 at its sp + 80).
constexpr uint32_t kBlitVertexCount = 4;
constexpr uint32_t kBlitVertexWords = 4;
using BlitQuad = std::array<uint32_t, kBlitVertexCount * kBlitVertexWords>;

// The blit's vertex address while sub_82183C68 runs on this thread; 0 otherwise.
thread_local uint32_t g_blit_vertices = 0;
// The quad last logged (raw words), per guest thread that draws the blit.
thread_local std::optional<BlitQuad> g_logged_quad;

// The Smoothing word Renut was last told (it starts with the game's shader, as for Off).
std::atomic<uint32_t> g_told_mode{futari::smoothing::kSmoothingOff};

const std::optional<uint32_t>& DevSmoothing() {
  static const std::optional<uint32_t> value = [] {
    const std::string text = REXCVAR_GET(futari_dev_smoothing);
    if (text.empty()) {
      return std::optional<uint32_t>{};
    }
    const std::optional<uint32_t> parsed = ParseDevSmoothing(text);
    if (!parsed) {
      REXLOG_WARN("futari_dev_smoothing = \"{}\" is not 0, 1 or 4; ignored", text);
    }
    return parsed;
  }();
  return value;
}

}  // namespace

namespace futari::smoothing {

void ApplyDevSmoothing(uint8_t* base, uint32_t display_block) {
  const std::optional<uint32_t>& mode = DevSmoothing();
  if (!mode) {
    return;
  }
  REX_STORE_U32(display_block + kDisplaySmoothing, *mode);
  static bool logged = false;
  if (!logged) {
    logged = true;
    REXLOG_INFO("Smoothing {} from futari_dev_smoothing; the save keeps its own", *mode);
  }
}

void NoteBlitDraw(uint8_t* base, uint32_t vertices, uint32_t count) {
  if (g_blit_vertices == 0 || vertices != g_blit_vertices || count != kBlitVertexCount || !DevSmoothing()) {
    return;
  }
  BlitQuad quad;
  for (uint32_t i = 0; i < quad.size(); ++i) {
    quad[i] = REX_LOAD_U32(vertices + 4 * i);
  }
  if (g_logged_quad == quad) {
    return;
  }
  g_logged_quad = quad;
  const auto f = [&quad](uint32_t i) { return std::bit_cast<float>(quad[i]); };
  REXLOG_INFO("Blit quad (x, y, u, v): ({}, {}, {}, {}) ({}, {}, {}, {}) ({}, {}, {}, {}) ({}, {}, {}, {})", f(0), f(1),
              f(2), f(3), f(4), f(5), f(6), f(7), f(8), f(9), f(10), f(11), f(12), f(13), f(14), f(15));
}

}  // namespace futari::smoothing

// The gameplay blit (r4 = its quad): Sharp runs as On, restored after unless the game changed it meanwhile.
REX_EXTERN(__imp__sub_82183C68);
extern "C" REX_FUNC(sub_82183C68) {
  const uint32_t block = REX_LOAD_U32(kDisplayBlockPointer);
  const uint32_t mode = block != 0 ? REX_LOAD_U32(block + kDisplaySmoothing) : 0;
#if FUTARI_RENUT_RENDERER
  if (block != 0 && banjo::renut_gpu::Active()) {
    if (g_told_mode.exchange(mode, std::memory_order_relaxed) != mode) {
      renut::gpu::SetBlitSmoothing(mode);
    }
  }
#endif
  const uint32_t draw_mode = BlitDrawMode(mode);
  const bool patched = block != 0 && draw_mode != mode;
  if (patched) {
    REX_STORE_U32(block + kDisplaySmoothing, draw_mode);
  }
  g_blit_vertices = ctx.r4.u32;
  __imp__sub_82183C68(ctx, base);
  g_blit_vertices = 0;
  if (patched && REX_LOAD_U32(block + kDisplaySmoothing) == draw_mode) {
    REX_STORE_U32(block + kDisplaySmoothing, mode);
  }
}
