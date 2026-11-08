// The size rules of the game's output-sized frame (src/screen_layout.cpp), pure std so a test can run them without
// the SDK (tests/screen_layout_test.cpp).
#pragma once

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>

namespace futari::screen_layout {

inline constexpr uint32_t kMinFrameWidth = 640;
inline constexpr uint32_t kMinFrameHeight = 480;
inline constexpr uint32_t kMaxFrameSide = 8192;

struct FrameSize {
  uint32_t width = 0;
  uint32_t height = 0;
  bool operator==(const FrameSize&) const = default;
};

// The frame the game draws for an output of w x h: 640..8192 wide, 480..8192 high.
constexpr FrameSize ClampFrame(uint32_t w, uint32_t h) {
  return {std::clamp(w, kMinFrameWidth, kMaxFrameSide), std::clamp(h, kMinFrameHeight, kMaxFrameSide)};
}

// The game's widescreen rule: 4:3 or wider. Portrait and anything narrower take its 4:3 branches.
constexpr bool IsWidescreen(uint32_t w, uint32_t h) { return uint64_t(w) * 3 >= uint64_t(h) * 4; }

// The dev size "WxH" (x or X, spaces around the parts): two whole numbers above 0, unclamped; else nothing.
inline std::optional<FrameSize> ParseFrameSize(std::string_view text) {
  const auto number = [](std::string_view part) -> std::optional<uint32_t> {
    const size_t first = part.find_first_not_of(" \t");
    if (first == std::string_view::npos) {
      return std::nullopt;
    }
    part = part.substr(first, part.find_last_not_of(" \t") - first + 1);
    uint32_t value = 0;
    const auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), value);
    if (error != std::errc{} || end != part.data() + part.size() || value == 0) {
      return std::nullopt;
    }
    return value;
  };
  const size_t x = text.find_first_of("xX");
  if (x == std::string_view::npos) {
    return std::nullopt;
  }
  const std::optional<uint32_t> width = number(text.substr(0, x));
  const std::optional<uint32_t> height = number(text.substr(x + 1));
  if (!width || !height) {
    return std::nullopt;
  }
  return FrameSize{*width, *height};
}

}  // namespace futari::screen_layout
