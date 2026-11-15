// Standalone test of src/smoothing_math.h (pure std, no SDK): the game's Smoothing modes and the dev value that
// forces one. Locally with any C++20 compiler:
//
//   clang++ -std=c++20 -Wall -Wextra -I src tests/smoothing_test.cpp -o smoothing_test
//
// The game draws its gameplay image with Smoothing 0 (Off) or 1 (On); 4 is Sharp bilinear, which the game draws as
// On and the renderer then redraws sharp. futari_dev_smoothing forces one of the three for captures.
#include "smoothing_math.h"

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

namespace {

using futari::smoothing::BlitDrawMode;
using futari::smoothing::kSmoothingOff;
using futari::smoothing::kSmoothingOn;
using futari::smoothing::kSmoothingSharp;
using futari::smoothing::ParseDevSmoothing;

int g_failed = 0;
int g_checks = 0;

void Check(bool ok, const char* what) {
  ++g_checks;
  if (ok)
    return;
  ++g_failed;
  std::printf("FAIL: %s\n", what);
}

// Sharp draws as On (the guest's linear filter and On shader pair); every other value is the game's own.
void SharpDrawsAsOn() {
  Check(BlitDrawMode(kSmoothingSharp) == kSmoothingOn, "Sharp draws as On");
  Check(BlitDrawMode(kSmoothingOn) == kSmoothingOn, "On stays On");
  Check(BlitDrawMode(kSmoothingOff) == kSmoothingOff, "Off stays Off");
  Check(BlitDrawMode(2) == 2, "the game's mode 2 stays");
  Check(BlitDrawMode(3) == 3, "the game's mode 3 stays");
  Check(BlitDrawMode(5) == 5, "an unknown value stays");
}

void CheckDevSmoothing(const char* text, std::optional<uint32_t> want, const char* what) {
  Check(ParseDevSmoothing(text) == want, what);
}

// The dev value: 0, 1 or 4, spaces around it allowed; else nothing.
void ParseDevSmoothingValue() {
  CheckDevSmoothing("0", kSmoothingOff, "0 is Off");
  CheckDevSmoothing("1", kSmoothingOn, "1 is On");
  CheckDevSmoothing("4", kSmoothingSharp, "4 is Sharp");
  CheckDevSmoothing(" 4\t", kSmoothingSharp, "spaces around the number");
  CheckDevSmoothing("2", std::nullopt, "2 is nothing");
  CheckDevSmoothing("3", std::nullopt, "3 is nothing");
  CheckDevSmoothing("5", std::nullopt, "5 is nothing");
  CheckDevSmoothing("-1", std::nullopt, "a negative number is nothing");
  CheckDevSmoothing("", std::nullopt, "empty is nothing");
  CheckDevSmoothing("  ", std::nullopt, "only spaces is nothing");
  CheckDevSmoothing("+4", std::nullopt, "a plus sign is nothing");
  CheckDevSmoothing("4x", std::nullopt, "trailing garbage is nothing");
  CheckDevSmoothing("sharp", std::nullopt, "a word is nothing");
  CheckDevSmoothing("4294967300", std::nullopt, "past 32 bits is nothing");
}

}  // namespace

int main() {
  SharpDrawsAsOn();
  ParseDevSmoothingValue();
  std::printf("smoothing_test: %d checks, %d failed\n", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
