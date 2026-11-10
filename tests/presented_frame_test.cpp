// Standalone test of src/renut_engine/gpu/backend/presented_frame.h (pure std): when the present copies the game's
// frame pixel for pixel (the point sampler) instead of scaling it. Locally with any C++20 compiler:
//
//   clang++ -std=c++20 -Wall -Wextra -I src tests/presented_frame_test.cpp -o presented_frame_test
#include "renut_engine/gpu/backend/presented_frame.h"

#include <cstdio>

namespace {

int g_failed = 0;
int g_checks = 0;

void Check(bool ok, const char* what) {
  ++g_checks;
  if (ok)
    return;
  ++g_failed;
  std::printf("FAIL: %s\n", what);
}

using renut::gpu::IsExactFit;

void EqualSizesAreExact() {
  Check(IsExactFit(1920, 1080, 1920, 1080), "1920x1080 in 1920x1080");
  Check(IsExactFit(1080, 1920, 1080, 1920), "portrait 1080x1920 in 1080x1920");
  Check(IsExactFit(1365, 767, 1365, 767), "odd size 1365x767");
  Check(IsExactFit(1, 1, 1, 1), "1x1");
}

void OneAxisOffByOneIsNot() {
  Check(!IsExactFit(1921, 1080, 1920, 1080), "output 1 wider");
  Check(!IsExactFit(1919, 1080, 1920, 1080), "output 1 narrower");
  Check(!IsExactFit(1920, 1081, 1920, 1080), "output 1 taller");
  Check(!IsExactFit(1920, 1079, 1920, 1080), "output 1 shorter");
  Check(!IsExactFit(2560, 1440, 1920, 1080), "a preset scaled up");
  Check(!IsExactFit(1080, 1920, 1920, 1080), "the sizes swapped");
}

void ZeroSizesAreNot() {
  Check(!IsExactFit(0, 0, 0, 0), "all zero");
  Check(!IsExactFit(0, 1080, 0, 1080), "zero width on both");
  Check(!IsExactFit(1920, 0, 1920, 0), "zero height on both");
  Check(!IsExactFit(0, 0, 1920, 1080), "minimized output");
  Check(!IsExactFit(1920, 1080, 0, 0), "no source");
}

}  // namespace

int main() {
  EqualSizesAreExact();
  OneAxisOffByOneIsNot();
  ZeroSizesAreNot();
  std::printf("presented_frame_test: %d checks, %d failed\n", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
