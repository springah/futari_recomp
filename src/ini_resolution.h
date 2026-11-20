// The [Display] resolution a futari.ini save writes (SaveFutariIni, src/main.cpp), pure std so a test can run the rule
// without the SDK (tests/ini_resolution_test.cpp).
#pragma once

#include <cctype>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace futari::ini_resolution {

// The [Display] resolution a fresh install's futari.ini has (WriteDefaultIni, src/main.cpp).
inline constexpr int32_t kDefaultWidth = 1920;
inline constexpr int32_t kDefaultHeight = 1080;

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

// One width or height as the boot reads it: a positive int32 in decimal digits, spaces around it ignored; empty
// otherwise (0, a sign, a fraction, hex, an overflow, other text).
inline std::optional<int32_t> ParseSize(std::string_view text) {
  const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
  while (!text.empty() && is_space(static_cast<unsigned char>(text.front())))
    text.remove_prefix(1);
  while (!text.empty() && is_space(static_cast<unsigned char>(text.back())))
    text.remove_suffix(1);
  int32_t value = 0;
  const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc() || end != text.data() + text.size() || value <= 0)
    return std::nullopt;
  return value;
}

// A [Display] resolution value as the boot reads it ("1920x1080", the x in either case); empty when the boot ignores it.
inline std::optional<std::pair<int32_t, int32_t>> ParseResolution(std::string_view value) {
  const size_t sep = value.find_first_of("xX");
  if (sep == std::string_view::npos)
    return std::nullopt;
  const auto width = ParseSize(value.substr(0, sep));
  const auto height = ParseSize(value.substr(sep + 1));
  if (!width || !height)
    return std::nullopt;
  return std::pair{*width, *height};
}

// The resolution to save (width, height; empty when the window_width/height cvars are absent), from those cvars' text.
// A size the launcher wrote since boot (picked) is a typed frame size and goes out as it is, even when it equals the
// boot logical size. Otherwise the cvars still at the boot logical size mean nothing changed them: the size as typed
// goes out. Any other value is taken as typed too. A width or height the next boot would ignore (0, the cvars' default:
// a hand-set window_width = 0, or an ini whose [Display] resolution the boot rejected; a negative or malformed one)
// saves nothing like an absent one. F4's "Reset all to defaults" leaves these hidden cvars alone.
inline std::pair<std::string, std::string> SavedResolution(std::string_view window_width, std::string_view window_height,
                                                           bool picked, const BootResolution& boot) {
  const auto width = ParseSize(window_width);
  const auto height = ParseSize(window_height);
  if (!width || !height) {
    return {};
  }
  if (!picked && boot.width && *width == boot.logical_width && *height == boot.logical_height) {
    return {std::to_string(boot.width), std::to_string(boot.height)};
  }
  return {std::to_string(*width), std::to_string(*height)};
}

// What a save writes over the file's [Display] resolution when SavedResolution has nothing (width, height; empty keeps
// the line): nothing when the line is absent or a size the boot uses. A line the boot rejects (0x0, a negative size,
// garbage) would never be fixed by a save, so it gets the size the window booted at in physical pixels (0 when unknown,
// e.g. a fullscreen boot: a fresh install's kDefaultWidth x kDefaultHeight).
inline std::pair<std::string, std::string> RepairedResolution(std::optional<std::string_view> file_value,
                                                              int32_t booted_width, int32_t booted_height) {
  if (!file_value || ParseResolution(*file_value)) {
    return {};
  }
  if (booted_width <= 0 || booted_height <= 0) {
    booted_width = kDefaultWidth;
    booted_height = kDefaultHeight;
  }
  return {std::to_string(booted_width), std::to_string(booted_height)};
}

}  // namespace futari::ini_resolution
