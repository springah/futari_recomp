// Dev aid: the names of the F9 frame captures (src/main.cpp), numbered files next to futari_renut_capture_path, and
// the PFM file of a 16-bit float source capture, pure std so a test can run them without the SDK
// (tests/capture_series_test.cpp).
#pragma once

#include <algorithm>
#include <bit>
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

// The presented picture taken with frame capture `number`: <stem>_NNN_<W>x<H>_output.ppm at the picture's own size.
// Not a number of the series (CaptureNumber ignores it).
inline std::string CaptureOutputFileName(std::string_view stem, uint32_t number, uint32_t width, uint32_t height) {
  std::string name = CaptureFileName(stem, number, width, height);
  return name.insert(name.size() - 4, "_output");
}

// The gameplay image the blit sampled in the frame of capture `number` (Renut's resolve of the game's own target):
// <stem>_NNN_source_<W>x<H>.ppm at that image's size. Not a number of the series (CaptureNumber ignores it).
inline std::string CaptureSourceFileName(std::string_view stem, uint32_t number, uint32_t width, uint32_t height) {
  std::string name = CaptureFileName(stem, number, width, height);
  return name.insert(name.rfind('_') + 1, "source_");  // the last '_' is the one before the size
}

// The same image's exact values when it is stored as 16-bit floats: <stem>_NNN_source_<W>x<H>.pfm (float32, PFM).
inline std::string CaptureSourceFloatFileName(std::string_view stem, uint32_t number, uint32_t width,
                                              uint32_t height) {
  std::string name = CaptureSourceFileName(stem, number, width, height);
  return name.replace(name.size() - 4, 4, ".pfm");
}

// A colour PFM file of float R G B texels given rows top to bottom: "PF", the size, scale -1 (little-endian), then
// the rows bottom to top as the format stores them. Empty when 'rgb' holds fewer than width * height * 3 values.
inline std::string PfmFile(uint32_t width, uint32_t height, const std::vector<float>& rgb) {
  const size_t row_values = size_t(width) * 3;
  if (rgb.size() < row_values * height) {
    return {};
  }
  std::string file = "PF\n" + std::to_string(width) + " " + std::to_string(height) + "\n-1.0\n";
  file.reserve(file.size() + rgb.size() * 4);
  for (uint32_t y = height; y-- > 0;) {
    for (size_t i = 0; i < row_values; ++i) {
      const uint32_t bits = std::bit_cast<uint32_t>(rgb[y * row_values + i]);
      for (int shift = 0; shift < 32; shift += 8) {
        file.push_back(char(uint8_t(bits >> shift)));
      }
    }
  }
  return file;
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
