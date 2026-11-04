// futari_recomp (2026-11-04, leg/rectlist): see futari_rect_timing.h.
#include "renut_engine/gpu/futari_rect_timing.h"

#include <rex/cvar.h>
#include <rex/logging.h>

#include <atomic>
#include <string>

REXCVAR_DEFINE_STRING(renut_rect_fallback, "tess", "Futari/Renut",
                      "Without geometry shaders: rectangle lists by 'tess' (tessellation) or 'plain' (one triangle "
                      "each, a wrong picture; for timing only)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(renut_draw_timing, false, "Futari/Renut",
                    "Dev: time Renut's per-draw work, binds, draw commands and submits (a 10 s log line)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace futari::renut_timing {
namespace {
std::atomic<uint64_t> g_ns[kPartCount];
std::atomic<uint64_t> g_calls[kPartCount];
std::atomic<uint64_t> g_draws, g_rect_draws, g_rect_tess_draws, g_binds;
}  // namespace

bool RectFallbackPlain() {
  static const bool plain = REXCVAR_GET(renut_rect_fallback) == "plain";
  return plain;
}

bool On() {
  static const bool on = REXCVAR_GET(renut_draw_timing);
  return on;
}

void Add(Part part, uint64_t ns) {
  g_ns[part].fetch_add(ns, std::memory_order_relaxed);
  g_calls[part].fetch_add(1, std::memory_order_relaxed);
}

void CountDraw(bool rectangle_list, bool tessellated) {
  if (!On())
    return;
  g_draws.fetch_add(1, std::memory_order_relaxed);
  if (rectangle_list) {
    g_rect_draws.fetch_add(1, std::memory_order_relaxed);
    if (tessellated)
      g_rect_tess_draws.fetch_add(1, std::memory_order_relaxed);
  }
}

void CountBind() {
  if (On())
    g_binds.fetch_add(1, std::memory_order_relaxed);
}

void Report(double seconds, uint64_t render_frames) {
  if (!On())
    return;
  const double frames = render_frames ? double(render_frames) : 1.0;
  double ms[kPartCount];
  uint64_t calls[kPartCount];
  for (uint32_t i = 0; i < kPartCount; ++i) {
    ms[i] = double(g_ns[i].exchange(0, std::memory_order_relaxed)) / 1e6 / frames;
    calls[i] = g_calls[i].exchange(0, std::memory_order_relaxed);
  }
  const uint64_t draws = g_draws.exchange(0), rects = g_rect_draws.exchange(0),
                 rects_tess = g_rect_tess_draws.exchange(0), binds = g_binds.exchange(0);
  REXLOG_INFO("[renut] draw timing ({:.1f} s, rect fallback {}): per render frame {:.2f} ms in draws ({:.0f} draws, "
              "{:.0f} rectangle lists, {:.0f} of them tessellated), {:.2f} ms pipeline binds ({:.0f} binds), {:.2f} ms "
              "draw commands, {:.2f} ms submits ({:.1f} submits)",
              seconds, RectFallbackPlain() ? "plain" : "tess", ms[kDraw], double(draws) / frames,
              double(rects) / frames, double(rects_tess) / frames, ms[kBind], double(binds) / frames, ms[kCommand],
              ms[kSubmit], double(calls[kSubmit]) / frames);
}

}  // namespace futari::renut_timing
