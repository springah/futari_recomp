// Dev aid: the names of the F9 frame captures (src/main.cpp), numbered files next to futari_renut_capture_path,
// pure std so a test can run them without the SDK (tests/capture_series_test.cpp).
#pragma once

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace futari::dev {

// <stem>_NNN_<W>x<H>.ppm, the number at least three digits wide.
inline std::string CaptureFileName(std::string_view stem, uint32_t number, uint32_t width, uint32_t height) {
  std::string digits = std::to_string(number);
  if (digits.size() < 3) {
    digits.insert(0, 3 - digits.size(), '0');
  }
  return std::string(stem) + "_" + digits + "_" + std::to_string(width) + "x" + std::to_string(height) + ".ppm";
}

// The number of a capture of this stem's series, or nothing for any other file name. Stem, 'x' and ".ppm" match in any
// ASCII case, as the Windows file system the series writes into does: a case variant is the same file.
inline std::optional<uint32_t> CaptureNumber(std::string_view stem, std::string_view name) {
  // Reads a whole number at the front of 'name' (at least 'digits' long) and drops it.
  const auto take = [&name](size_t digits) -> std::optional<uint32_t> {
    uint32_t value = 0;
    const auto [end, error] = std::from_chars(name.data(), name.data() + name.size(), value);
    const size_t length = size_t(end - name.data());
    if (error != std::errc{} || length < digits) {
      return std::nullopt;
    }
    name.remove_prefix(length);
    return value;
  };
  const auto skip = [&name](std::string_view text) {
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; };
    if (name.size() < text.size() ||
        !std::equal(text.begin(), text.end(), name.begin(), [&](char a, char b) { return lower(a) == lower(b); })) {
      return false;
    }
    name.remove_prefix(text.size());
    return true;
  };
  if (!skip(stem) || !skip("_")) {
    return std::nullopt;
  }
  const std::optional<uint32_t> number = take(3);
  if (!number || !skip("_") || !take(1) || !skip("x") || !take(1) || !skip(".ppm") || !name.empty()) {
    return std::nullopt;
  }
  return number;
}

// The number after the highest capture of this stem among the folder's file names (1 when there is none), or nothing
// when the series is at the largest number.
inline std::optional<uint32_t> NextCaptureNumber(std::string_view stem, const std::vector<std::string>& names) {
  uint32_t highest = 0;
  for (const std::string& name : names) {
    highest = std::max(highest, CaptureNumber(stem, name).value_or(0));
  }
  if (highest == UINT32_MAX) {
    return std::nullopt;
  }
  return highest + 1;
}

// The first number from 'number' up for which taken(n) is false (the file the capture would be written to is not
// there), or nothing when every number up to the largest is taken. The series never opens an existing file.
template <typename Taken>
std::optional<uint32_t> FreeCaptureNumber(uint32_t number, const Taken& taken) {
  while (taken(number)) {
    if (number == UINT32_MAX) {
      return std::nullopt;
    }
    ++number;
  }
  return number;
}

}  // namespace futari::dev
