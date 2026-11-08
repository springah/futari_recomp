// The game's frame at the output size (src/screen_layout.cpp): what the app tells it about the output.
#pragma once

#include <cstdint>

namespace futari::screen_layout {

// [Display] resolution as typed, in physical pixels: the boot size until the window reports its own.
void SetConfiguredSize(uint32_t width, uint32_t height);
// The window's client area in physical pixels (the monitor's size in fullscreen), on the UI thread at every change.
// 0x0 (minimised) is ignored.
void SetWindowPixelSize(uint32_t width, uint32_t height);

}  // namespace futari::screen_layout
