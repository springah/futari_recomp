// The game's Smoothing modes (the display block's +68), pure std so a test can run it without the SDK
// (tests/smoothing_test.cpp).
#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>
#include <system_error>

namespace futari::smoothing {

// The game's own values, and the new Sharp bilinear: the game's blit skips its filter code for any value above 3.
inline constexpr uint32_t kSmoothingOff = 0;
inline constexpr uint32_t kSmoothingOn = 1;
inline constexpr uint32_t kSmoothingSharp = 4;

// The value the game's blit runs with: Sharp draws as On (linear filter, the On shader pair), which the renderer then
// redraws sharp, or which stays On where it cannot; every other value is the game's own.
constexpr uint32_t BlitDrawMode(uint32_t mode) { return mode == kSmoothingSharp ? kSmoothingOn : mode; }

// The dev value futari_dev_smoothing: 0, 1 or 4, spaces around it allowed; else nothing.
inline std::optional<uint32_t> ParseDevSmoothing(std::string_view text) {
  const size_t first = text.find_first_not_of(" \t");
  if (first == std::string_view::npos) {
    return std::nullopt;
  }
  text = text.substr(first, text.find_last_not_of(" \t") - first + 1);
  uint32_t value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size() ||
      (value != kSmoothingOff && value != kSmoothingOn && value != kSmoothingSharp)) {
    return std::nullopt;
  }
  return value;
}

}  // namespace futari::smoothing
