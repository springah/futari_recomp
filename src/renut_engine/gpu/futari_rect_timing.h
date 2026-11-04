// futari_recomp (2026-11-04, leg/rectlist): the rectangle-list fallback choice and a dev measurement aid.
//
// renut_rect_fallback (init-only): on devices without geometry shaders, "tess" (default) draws rectangle lists by
// tessellation (rectangle_list_hs/ds), "plain" by the old one triangle per rectangle (wrong picture, same draws), so
// the two can be timed against each other.
//
// renut_draw_timing (dev, off by default): render-thread time in Renut's per-draw work (DrawStateLocked), the pipeline
// binds, the draw commands and the queue submits, plus draw / rectangle-list / bind counts, as a "[renut] draw timing"
// line every 10 s after the "[renut] 10.0 s" one.
#pragma once

#include <chrono>
#include <cstdint>

namespace futari::renut_timing {

bool RectFallbackPlain();
bool On();

enum Part : uint32_t { kDraw, kBind, kCommand, kSubmit, kPartCount };
void Add(Part part, uint64_t ns);
void CountDraw(bool rectangle_list, bool tessellated);
void CountBind();
void Report(double seconds, uint64_t render_frames);

class Scope {
 public:
  explicit Scope(Part part) : part_(part), on_(On()) {
    if (on_)
      start_ = std::chrono::steady_clock::now();
  }
  ~Scope() {
    if (on_)
      Add(part_, uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() -
                                                                                 start_)
                              .count()));
  }

 private:
  Part part_;
  bool on_;
  std::chrono::steady_clock::time_point start_;
};

}  // namespace futari::renut_timing
