// The game's Smoothing modes (the display block's +68), pure std so a test can run it without the SDK
// (tests/smoothing_test.cpp).
#pragma once

#include <algorithm>
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

// The Picture page's Smoothing item holds a menu index, not the word: Smooth 0, Sharp 1, Off 2, so the game's row
// step (right +1, ends clamp) goes Smooth -> Sharp -> Off. The game's unused linear modes 2 and 3 show as Smooth,
// any other unknown word as Off.
constexpr uint32_t SmoothingIndex(uint32_t word) {
  switch (word) {
    case kSmoothingOn:
    case 2:
    case 3:
      return 0;
    case kSmoothingSharp:
      return 1;
    default:
      return 2;
  }
}

// The word an index stores: 0 On, 1 Sharp, anything else Off.
constexpr uint32_t SmoothingWord(uint32_t index) {
  switch (index) {
    case 0:
      return kSmoothingOn;
    case 1:
      return kSmoothingSharp;
    default:
      return kSmoothingOff;
  }
}

// The word the page stores for an index: the stored word when it already shows as that index (the game stores the
// row once without input after each load, which must not turn a 2 or 3 into 1), else the index's own word.
constexpr uint32_t SmoothingStore(uint32_t index, uint32_t stored) {
  return SmoothingIndex(stored) == index ? stored : SmoothingWord(index);
}

// The default Smoothing word: Sharp with Renut, the game's On elsewhere (where Sharp would draw as On anyway).
constexpr uint32_t ResetSmoothing(bool renut) { return renut ? kSmoothingSharp : kSmoothingOn; }

// The item's label for an index: ASCII, at most 15 bytes (it is written into the game's short-string buffer).
// Off has none: it keeps the game's own "Off".
constexpr std::optional<std::string_view> SmoothingLabel(uint32_t index) {
  switch (index) {
    case 0:
      return std::string_view("Smooth");
    case 1:
      return std::string_view("Sharp");
    default:
      return std::nullopt;
  }
}

// The Smoothing item's labels as its load finds them: the game's two (labels 0 and 1 read exactly "Off" and "On"),
// our three (a reload of an item already relabelled: labels 0 and 1 hold our texts), or anything else. `plain` =
// every label is a short string with no nested labels of its own; `first` and `second` are the texts of labels 0
// and 1 (empty where there is none).
enum class SmoothingLabelSet { kGames, kOurs, kUnexpected };

constexpr SmoothingLabelSet FindSmoothingLabels(uint32_t count, bool plain, std::string_view first,
                                                std::string_view second) {
  if (!plain) {
    return SmoothingLabelSet::kUnexpected;
  }
  switch (count) {
    case 2:
      return first == "Off" && second == "On" ? SmoothingLabelSet::kGames : SmoothingLabelSet::kUnexpected;
    case 3:
      return first == SmoothingLabel(0) && second == SmoothingLabel(1) ? SmoothingLabelSet::kOurs
                                                                        : SmoothingLabelSet::kUnexpected;
    default:
      return SmoothingLabelSet::kUnexpected;
  }
}

// The label index when the item keeps the game's labels: Off for 0, On for any other word, always below the label
// count (0 when there are none).
constexpr uint32_t SmoothingFallbackIndex(uint32_t word, uint32_t count) {
  const uint32_t index = word == kSmoothingOff ? 0 : 1;
  return count == 0 ? 0 : std::min(index, count - 1);
}

}  // namespace futari::smoothing
