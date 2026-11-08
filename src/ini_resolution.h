// The [Display] resolution a futari.ini save writes (SaveFutariIni, src/main.cpp), pure std so a test can run the rule
// without the SDK (tests/ini_resolution_test.cpp).
#pragma once

#include <cstdint>
#include <string>
#include <utility>

namespace futari::ini_resolution {

// The [Display] resolution read at boot: the size as typed (physical pixels) and the logical size window_width/height
// got from it at the primary display's scale (and again at the window's own, once it exists). All 0 when the line was
// absent or unreadable.
struct BootResolution {
  int32_t width = 0;
  int32_t height = 0;
  int32_t logical_width = 0;
  int32_t logical_height = 0;
};

// A typed size (physical pixels) and the window cvars for it at a DPI: the SDK's SizeToLogical (ConvertSizeDpi to 96,
// rounded up). An unknown DPI (0) counts as 96.
constexpr BootResolution BootAtDpi(int32_t width, int32_t height, uint32_t dpi) {
  const int64_t d = dpi ? dpi : 96;
  return {width, height, int32_t((int64_t(width) * 96 + d - 1) / d), int32_t((int64_t(height) * 96 + d - 1) / d)};
}

// The resolution to save (width, height; empty when the window_width/height cvars are absent), from those cvars' text.
// A size the launcher wrote since boot (picked) is a typed frame size and goes out as it is, even when it equals the
// boot logical size. Otherwise the cvars still at the boot logical size mean nothing changed them: the size as typed
// goes out. Any other value (an F4 edit, which sets no flag) is taken as typed too.
inline std::pair<std::string, std::string> SavedResolution(std::string window_width, std::string window_height,
                                                           bool picked, const BootResolution& boot) {
  if (window_width.empty() || window_height.empty()) {
    return {};
  }
  if (!picked && boot.width && window_width == std::to_string(boot.logical_width) &&
      window_height == std::to_string(boot.logical_height)) {
    return {std::to_string(boot.width), std::to_string(boot.height)};
  }
  return {std::move(window_width), std::move(window_height)};
}

}  // namespace futari::ini_resolution
