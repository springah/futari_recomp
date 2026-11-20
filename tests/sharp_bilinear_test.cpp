// Standalone test of src/sharp_bilinear_math.h (pure std, no SDK): the Sharp bilinear filter of the gameplay image.
// Locally with any C++20 compiler:
//
//   clang++ -std=c++20 -Wall -Wextra -I src tests/sharp_bilinear_test.cpp -o sharp_bilinear_test
//
// Sharp bilinear is defined as a nearest-neighbour prescale of the image by k = max(1, floor(scale)) per axis,
// then bilinear (clamp addressing) of that prescaled image to the destination. The filter gets the same result
// from the source image alone, by moving each sample to its texel's centre except in a band of half a prescaled
// texel at the texel's edges. Each source pixel becomes a solid block; an integer scale is nearest neighbour and
// a scale below 1 is plain bilinear. A rotated (tate) screen passes each source axis its own scale.
#include "sharp_bilinear_math.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <string>

namespace {

using futari::sharp_bilinear::Image;
using futari::sharp_bilinear::NearestPrescale;
using futari::sharp_bilinear::Rgb;
using futari::sharp_bilinear::SampleBilinear;
using futari::sharp_bilinear::SampleSharp;
using futari::sharp_bilinear::SampleSharpDefinition;
using futari::sharp_bilinear::SharpK;

int g_failed = 0;
int g_checks = 0;

void Check(bool ok, const char* what, const std::string& got) {
  ++g_checks;
  if (ok)
    return;
  ++g_failed;
  std::printf("FAIL: %s\n---- got ----\n%s\n---- end ----\n", what, got.c_str());
}

constexpr float kTolerance = 1.0f / 255.0f;

constexpr uint8_t kLevels[] = {0, 255, 0, 255, 37, 128, 200};

// A fixed 8-bit image with hard edges: each channel is black, white or one of a few levels, picked by an LCG.
Image TestImage(int width, int height, uint32_t seed) {
  Image image{width, height, {}};
  image.texels.resize(size_t(width) * size_t(height));
  uint32_t state = seed;
  const auto next = [&state] {
    state = state * 1664525u + 1013904223u;
    return kLevels[(state >> 16) % std::size(kLevels)] / 255.0f;
  };
  for (Rgb& texel : image.texels)
    texel = {next(), next(), next()};
  return image;
}

float MaxDifference(Rgb a, Rgb b) {
  return std::max({std::fabs(a.r - b.r), std::fabs(a.g - b.g), std::fabs(a.b - b.b)});
}

// Runs check(u, v, label) at every destination pixel centre of the image scaled by scale_x x scale_y.
template <typename F>
void OverDestination(const Image& image, float scale_x, float scale_y, F check) {
  const int width = std::max(1, int(std::lround(image.width * scale_x)));
  const int height = std::max(1, int(std::lround(image.height * scale_y)));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x)
      check((x + 0.5f) / width, (y + 0.5f) / height);
  }
}

// The largest per-channel difference between the filter and the definition over the destination grid.
float WorstAgainstDefinition(const Image& image, float scale_x, float scale_y) {
  const Image prescaled = NearestPrescale(image, SharpK(scale_x), SharpK(scale_y));
  float worst = 0.0f;
  OverDestination(image, scale_x, scale_y, [&](float u, float v) {
    worst = std::max(worst, MaxDifference(SampleSharp(image, u, v, scale_x, scale_y),
                                          SampleSharpDefinition(prescaled, u, v)));
  });
  return worst;
}

std::string Label(float scale_x, float scale_y, float worst) {
  char text[160];
  std::snprintf(text, sizeof text, "at %gx%g: worst difference %g", scale_x, scale_y, worst);
  return text;
}

void CheckDefinition(const Image& image, float scale_x, float scale_y, const char* what) {
  const float worst = WorstAgainstDefinition(image, scale_x, scale_y);
  Check(worst <= kTolerance, what, Label(scale_x, scale_y, worst));
}

void CheckK(float scale, int want, const char* what) {
  Check(SharpK(scale) == want, what, std::to_string(SharpK(scale)));
}

void SharpKFromTheScale() {
  CheckK(0.75f, 1, "a scale below 1 gives k 1");
  CheckK(1.0f, 1, "scale 1 gives k 1");
  CheckK(1.5f, 1, "scale 1.5 gives k 1");
  CheckK(2.25f, 2, "scale 2.25 gives k 2");
  CheckK(2.999f, 3, "scale 2.999 (rounding noise below 3) gives k 3");
  CheckK(3.0f, 3, "scale 3 gives k 3");
  CheckK(3.375f, 3, "scale 3.375 gives k 3");
  CheckK(0.0f, 1, "scale 0 gives k 1");
  CheckK(-2.0f, 1, "a negative scale gives k 1");
}

// The shader caps the scale at 4096 (a degenerate derivative gives it an infinite one); k is the same here.
void SharpKCappedAsTheShader() {
  CheckK(4096.0f, 4096, "scale 4096 gives k 4096");
  CheckK(5000.0f, 4096, "scale 5000 is capped at 4096");
  CheckK(1e6f, 4096, "scale 1e6 is capped at 4096");
  CheckK(3e9f, 4096, "scale 3e9 (past int) is capped at 4096");
  CheckK(std::numeric_limits<float>::infinity(), 4096, "an infinite scale is capped at 4096");
  CheckK(std::numeric_limits<float>::quiet_NaN(), 1, "a NaN scale gives k 1");
}

void FilterMatchesTheDefinition() {
  const Image image = TestImage(7, 5, 12345u);
  for (const float scale : {1.0f, 1.5f, 2.25f, 3.375f, 0.75f})
    CheckDefinition(image, scale, scale, "equal axes");
}

void FilterMatchesTheDefinitionOnUnequalAxes() {
  const Image image = TestImage(6, 9, 777u);
  CheckDefinition(image, 2.25f, 3.375f, "unequal axes");
  CheckDefinition(image, 3.375f, 2.25f, "the axes swapped (tate)");
  CheckDefinition(image, 1.5f, 3.0f, "a fractional and an integer axis");
  CheckDefinition(image, 0.75f, 2.25f, "one axis below 1");
}

// A rotated screen: destination x runs along source v (downwards), destination y along source u, each source
// axis with its own scale.
void FilterMatchesTheDefinitionRotated() {
  const Image image = TestImage(5, 8, 4242u);
  const float scale_u = 3.375f;
  const float scale_v = 2.25f;
  const Image prescaled = NearestPrescale(image, SharpK(scale_u), SharpK(scale_v));
  const int width = int(std::lround(image.height * scale_v));
  const int height = int(std::lround(image.width * scale_u));
  float worst = 0.0f;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const float u = (y + 0.5f) / height;
      const float v = 1.0f - (x + 0.5f) / width;
      worst = std::max(worst, MaxDifference(SampleSharp(image, u, v, scale_u, scale_v),
                                            SampleSharpDefinition(prescaled, u, v)));
    }
  }
  Check(worst <= kTolerance, "rotated 90 degrees", Label(scale_u, scale_v, worst));
}

Rgb Nearest(const Image& image, float u, float v) {
  const int x = std::clamp(int(std::floor(u * image.width)), 0, image.width - 1);
  const int y = std::clamp(int(std::floor(v * image.height)), 0, image.height - 1);
  return image.texels[size_t(y) * size_t(image.width) + size_t(x)];
}

void IntegerScaleIsNearest() {
  const Image image = TestImage(7, 5, 99u);
  for (const float scale : {1.0f, 2.0f, 3.0f}) {
    float worst = 0.0f;
    OverDestination(image, scale, scale, [&](float u, float v) {
      worst = std::max(worst, MaxDifference(SampleSharp(image, u, v, scale, scale), Nearest(image, u, v)));
    });
    Check(worst <= 1e-4f, "nearest neighbour", Label(scale, scale, worst));
  }
  float worst = 0.0f;
  OverDestination(image, 2.0f, 3.0f, [&](float u, float v) {
    worst = std::max(worst, MaxDifference(SampleSharp(image, u, v, 2.0f, 3.0f), Nearest(image, u, v)));
  });
  Check(worst <= 1e-4f, "nearest neighbour on unequal axes", Label(2.0f, 3.0f, worst));
}

void ScaleBelowOneIsBilinear() {
  const Image image = TestImage(8, 6, 31337u);
  for (const float scale : {0.75f, 0.5f}) {
    float worst = 0.0f;
    OverDestination(image, scale, scale, [&](float u, float v) {
      const Rgb plain = SampleBilinear(image, u * image.width, v * image.height);
      worst = std::max(worst, MaxDifference(SampleSharp(image, u, v, scale, scale), plain));
    });
    Check(worst <= 1e-6f, "plain bilinear", Label(scale, scale, worst));
  }
}

}  // namespace

int main() {
  SharpKFromTheScale();
  SharpKCappedAsTheShader();
  FilterMatchesTheDefinition();
  FilterMatchesTheDefinitionOnUnequalAxes();
  FilterMatchesTheDefinitionRotated();
  IntegerScaleIsNearest();
  ScaleBelowOneIsBilinear();
  std::printf("sharp_bilinear_test: %d checks, %d failed\n", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
