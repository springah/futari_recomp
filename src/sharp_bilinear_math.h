// The Sharp bilinear filter of the gameplay image, pure std so a test can run it without the SDK
// (tests/sharp_bilinear_test.cpp). The host pixel shader does the same per axis on the GPU.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace futari::sharp_bilinear {

struct Rgb {
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
};

// An RGB image, rows top to bottom, channels 0..1.
struct Image {
  int width = 0;
  int height = 0;
  std::vector<Rgb> texels;

  // Clamp addressing: coordinates outside the image take its edge texel.
  Rgb At(int x, int y) const {
    x = std::clamp(x, 0, width - 1);
    y = std::clamp(y, 0, height - 1);
    return texels[size_t(y) * size_t(width) + size_t(x)];
  }
};

// The largest scale k is taken from: a degenerate derivative gives the shader an infinite scale, and k stays finite.
// The same value as kMaxScale in src/renut_engine/gpu/shaders/host/gameplay_blit.hlsli; keep the two the same.
constexpr float kMaxScale = 4096.0f;

// The integer prescale of an axis scaled by scale: max(1, floor(min(scale, kMaxScale))), with 1e-3 of slack so a
// scale that rounding left just below a whole number (2.999...) counts as that number. NaN gives 1.
inline int SharpK(float scale) {
  if (!(scale > 1.0f))
    return 1;
  return int(std::floor(std::min(scale, kMaxScale) + 1e-3f));
}

// Moves texel coordinate t (texel i covers [i, i+1), its centre at i + 0.5) to its texel's centre, except in a
// band of half a prescaled texel (0.5/k) at each edge, where it moves k times faster towards the neighbour's
// centre: bilinear at the result equals bilinear of the image prescaled by k (nearest) at t.
inline float SharpTexel(float t, int k) {
  const float cell = std::floor(t);
  const float c = t - cell - 0.5f;
  const float r = 0.5f - 0.5f / float(k);
  return cell + (c - std::clamp(c, -r, r)) * float(k) + 0.5f;
}

// Bilinear at texel coordinates (x, y), texel centres at i + 0.5, clamp addressing.
inline Rgb SampleBilinear(const Image& image, float x, float y) {
  const float sx = x - 0.5f;
  const float sy = y - 0.5f;
  const float x0f = std::floor(sx);
  const float y0f = std::floor(sy);
  const float fx = sx - x0f;
  const float fy = sy - y0f;
  const int x0 = int(x0f);
  const int y0 = int(y0f);
  const auto mix = [](Rgb a, Rgb b, float f) {
    return Rgb{a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f};
  };
  const Rgb top = mix(image.At(x0, y0), image.At(x0 + 1, y0), fx);
  const Rgb bottom = mix(image.At(x0, y0 + 1), image.At(x0 + 1, y0 + 1), fx);
  return mix(top, bottom, fy);
}

// Sharp bilinear at normalised (u, v), each source axis drawn at its own scale (a rotated screen passes the
// scale of the source axis, not of the screen axis).
inline Rgb SampleSharp(const Image& image, float u, float v, float scale_x, float scale_y) {
  return SampleBilinear(image, SharpTexel(u * float(image.width), SharpK(scale_x)),
                        SharpTexel(v * float(image.height), SharpK(scale_y)));
}

// The definition's first half: each texel becomes a kx x ky block (nearest neighbour).
inline Image NearestPrescale(const Image& image, int kx, int ky) {
  Image out{image.width * kx, image.height * ky, {}};
  out.texels.reserve(size_t(out.width) * size_t(out.height));
  for (int y = 0; y < out.height; ++y) {
    for (int x = 0; x < out.width; ++x)
      out.texels.push_back(image.At(x / kx, y / ky));
  }
  return out;
}

// The definition's second half: bilinear of the prescaled image at normalised (u, v).
inline Rgb SampleSharpDefinition(const Image& prescaled, float u, float v) {
  return SampleBilinear(prescaled, u * float(prescaled.width), v * float(prescaled.height));
}

}  // namespace futari::sharp_bilinear
