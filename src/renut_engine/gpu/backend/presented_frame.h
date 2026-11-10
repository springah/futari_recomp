// futari_recomp (2026-11-08): where the last present put the game's frame, for an app's own UI (a hint kept inside
// the picture rather than over a bar). Pure std, so an app without plume's include path can ask.
#pragma once

#include <cstdint>

namespace renut::gpu {

struct PresentedFrame {
  // The frame's rectangle in output pixels.
  uint32_t x = 0;
  uint32_t y = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  // The output (swapchain) size it was placed in.
  uint32_t output_width = 0;
  uint32_t output_height = 0;
};

// Any thread; never waits for the render thread. All zero before the first frame is scanned out (or with another
// renderer), and also while a publish is in flight (it retries 64 times, then gives up).
PresentedFrame LastPresentedFrame();

}  // namespace renut::gpu
