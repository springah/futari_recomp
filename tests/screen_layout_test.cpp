// Standalone test of src/screen_layout_math.h (pure std, no SDK): the size rules
// the game's output-sized frame follows. Runs in CI (Guards, "screen-layout-test")
// on g++, and locally with any C++20 compiler:
//
//   g++ -std=c++20 -Wall -Wextra -I src tests/screen_layout_test.cpp -o screen_layout_test && ./screen_layout_test
//   cl /std:c++20 /EHsc /W4 /I src tests\screen_layout_test.cpp && screen_layout_test.exe
//
// The frame is clamped to 640..8192 wide and 480..8192 high (the smallest frame
// is 640x480), and the game's widescreen rule is "4:3 or wider": portrait and
// anything narrower take its 4:3 branches. The dev size futari_dev_frame_size is
// "WxH" (x or X, spaces allowed around the parts); anything else is ignored.
#include "screen_layout_math.h"

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

namespace {

using futari::screen_layout::ClampFrame;
using futari::screen_layout::FrameSize;
using futari::screen_layout::IsWidescreen;
using futari::screen_layout::ParseFrameSize;

int g_failed = 0;
int g_checks = 0;

void Check(bool ok, const char* what, const std::string& got) {
  ++g_checks;
  if (ok)
    return;
  ++g_failed;
  std::printf("FAIL: %s\n---- got ----\n%s\n---- end ----\n", what, got.c_str());
}

std::string Text(FrameSize size) { return std::to_string(size.width) + "x" + std::to_string(size.height); }

void CheckClamp(uint32_t w, uint32_t h, FrameSize want, const char* what) {
  const FrameSize got = ClampFrame(w, h);
  Check(got == want, what, Text(got));
}

void ClampAtAndAroundTheBounds() {
  CheckClamp(640, 480, {640, 480}, "640x480, the smallest frame, unchanged");
  CheckClamp(639, 479, {640, 480}, "639x479 raised to 640x480");
  CheckClamp(641, 481, {641, 481}, "641x481 unchanged");
  CheckClamp(639, 1080, {640, 1080}, "a width below 640 is raised alone");
  CheckClamp(1920, 479, {1920, 480}, "a height below 480 is raised alone");
  CheckClamp(480, 640, {640, 640}, "portrait 480x640: the width floor is 640");
  CheckClamp(8192, 8192, {8192, 8192}, "8192x8192 unchanged");
  CheckClamp(8193, 8193, {8192, 8192}, "8193x8193 lowered to 8192x8192");
  CheckClamp(8192, 1080, {8192, 1080}, "8192x1080 unchanged");
}

void ClampZeroAndHuge() {
  CheckClamp(0, 0, {640, 480}, "0x0 becomes the smallest frame");
  CheckClamp(UINT32_MAX, UINT32_MAX, {8192, 8192}, "the largest u32 becomes 8192x8192");
  CheckClamp(0, UINT32_MAX, {640, 8192}, "0 wide, huge high");
}

void ClampKeepsEveryListedSize() {
  const FrameSize sizes[] = {{1280, 720},  {1600, 900},  {1920, 1080}, {1920, 1200}, {2560, 1080},
                             {2560, 1440}, {3840, 2160}, {2520, 1680}, {1080, 1920}};
  for (const FrameSize size : sizes)
    CheckClamp(size.width, size.height, size, "a supported output size is unchanged");
}

void CheckWidescreen(uint32_t w, uint32_t h, bool want, const char* what) {
  const bool got = IsWidescreen(w, h);
  Check(got == want, what, got ? "true" : "false");
}

void Widescreen() {
  CheckWidescreen(640, 480, true, "640x480 (exactly 4:3) is widescreen");
  CheckWidescreen(1024, 768, true, "1024x768 (exactly 4:3) is widescreen");
  CheckWidescreen(1023, 768, false, "1023x768 (just below 4:3) is not");
  CheckWidescreen(1280, 961, false, "1280x961 (just below 4:3) is not");
  CheckWidescreen(1920, 1080, true, "1920x1080 (16:9) is widescreen");
  CheckWidescreen(2520, 1680, true, "2520x1680 (3:2) is widescreen");
  CheckWidescreen(1080, 1920, false, "portrait 1080x1920 is not");
  CheckWidescreen(8192, 6144, true, "8192x6144 (4:3 at the bound) is widescreen");
  CheckWidescreen(8192, 8192, false, "8192x8192 (square) is not");
}

std::string Text(std::optional<FrameSize> size) { return size ? Text(*size) : std::string("nothing"); }

void CheckParse(const char* text, std::optional<FrameSize> want, const char* what) {
  const std::optional<FrameSize> got = ParseFrameSize(text);
  Check(got == want, what, Text(got));
}

void ParseDevFrameSize() {
  CheckParse("1600x900", FrameSize{1600, 900}, "1600x900");
  CheckParse("1080x1920", FrameSize{1080, 1920}, "portrait 1080x1920");
  CheckParse("3840x2160", FrameSize{3840, 2160}, "3840x2160");
  CheckParse("1920X1200", FrameSize{1920, 1200}, "an upper-case X");
  CheckParse("  2560 x 1080\t", FrameSize{2560, 1080}, "spaces around and between the parts");
  CheckParse("", std::nullopt, "empty is nothing");
  CheckParse("   ", std::nullopt, "only spaces is nothing");
  CheckParse("0x0", std::nullopt, "0x0 is nothing");
  CheckParse("1920x0", std::nullopt, "a zero height is nothing");
  CheckParse("big", std::nullopt, "garbage is nothing");
  CheckParse("1920x1080p", std::nullopt, "trailing garbage is nothing");
  CheckParse("19 20x1080", std::nullopt, "a space inside a number is nothing");
  CheckParse("-1920x1080", std::nullopt, "a sign is nothing");
  CheckParse("1920x", std::nullopt, "a missing height is nothing");
  CheckParse("x1080", std::nullopt, "a missing width is nothing");
  CheckParse("1920", std::nullopt, "a missing x is nothing");
  CheckParse("1920x1080x2", std::nullopt, "a third part is nothing");
  CheckParse("99999999999x1080", std::nullopt, "a width past 32 bits is nothing");
  CheckParse("4294967295x4294967295", FrameSize{UINT32_MAX, UINT32_MAX}, "the largest u32 parses");
}

void ParsedSizesAreClamped() {
  Check(ClampFrame(4294967295u, 4294967295u) == FrameSize{8192, 8192}, "a huge parsed size is lowered to 8192x8192",
        Text(ClampFrame(4294967295u, 4294967295u)));
  const FrameSize small = *ParseFrameSize("320x240");
  Check(ClampFrame(small.width, small.height) == FrameSize{640, 480}, "320x240 is raised to 640x480",
        Text(ClampFrame(small.width, small.height)));
  const FrameSize wide = *ParseFrameSize("9000x1080");
  Check(ClampFrame(wide.width, wide.height) == FrameSize{8192, 1080}, "9000x1080 is lowered to 8192x1080",
        Text(ClampFrame(wide.width, wide.height)));
}

}  // namespace

int main() {
  ClampAtAndAroundTheBounds();
  ClampZeroAndHuge();
  ClampKeepsEveryListedSize();
  Widescreen();
  ParseDevFrameSize();
  ParsedSizesAreClamped();
  std::printf("screen_layout_test: %d checks, %d failed\n", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
