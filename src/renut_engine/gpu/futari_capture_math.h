// futari_recomp (2026-11-15): the conversions of the dev capture's 16-bit float source image (backend/present.cpp),
// pure std, so tests/capture_series_test.cpp checks them on their own.
#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

namespace renut::gpu::futari_capture {

// An IEEE 754 half as a float, exactly (subnormals, infinities and NaN included).
inline float HalfToFloat(uint16_t half) {
  const uint32_t sign = uint32_t(half >> 15) << 31;
  const uint32_t exponent = (half >> 10) & 0x1F;
  const uint32_t mantissa = half & 0x3FF;
  if (exponent == 0) {  // zero or subnormal: mantissa * 2^-24
    const float value = std::ldexp(float(mantissa), -24);
    return sign ? -value : value;
  }
  const uint32_t bits = exponent == 0x1F ? sign | 0x7F800000u | (mantissa << 13)
                                         : sign | ((exponent + 112) << 23) | (mantissa << 13);
  return std::bit_cast<float>(bits);
}

// A 0..1 channel as a byte: clamped, then the nearest of 255 steps (NaN is 0).
inline uint8_t UnitToByte(float value) {
  if (!(value > 0.0f)) {
    return 0;
  }
  return uint8_t(std::lround(std::min(value, 1.0f) * 255.0f));
}

}  // namespace renut::gpu::futari_capture
