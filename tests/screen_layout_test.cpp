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
//
// The menu wallpaper covers the frame at every aspect: a centred 16:9 quad, as
// small as covers W x H, grown by the game's slide-in offset d the way its own
// wide quad is (x by d on each side, y by 9d/16). At 16:9 it is the game's quad.
//
// The menu canvas: frames 4:3 or wider keep the game's 720-high design (scale
// H/720); narrower frames show the 960-wide content of its 4:3 layout across the
// frame width (design height 960*H/W, scale W/960), centred by the console's 4:3
// offset 320. Both agree at exactly 4:3.
//
// The exit hint's corner sits 16 display units inside the presented frame's
// bottom-right corner (output pixels scaled to the UI's display units), and at
// the window's corner when there is no frame (or a zero-size one).
//
// A full-screen picture (an opaque 1280x720 image drawn over the whole design
// canvas, the boot logos and the title) is surrounded by 8 bands out to beyond
// the frame: the logo's bands repeat its edge texels in its own colours (the
// fade and the lines run to the frame's edges), every other picture's are black.
// The title's background (the first picture after the title loads) instead
// covers the frame: its corners scale about the centre by the smallest factor,
// at least 1, that covers the frame's design canvas; texels and colours stay.
// The logo's texture is checked first, then the title's; the title's load ends
// a logo that has drawn no picture yet.
//
// A live resize writes the values a boot at the new size would (block, floats,
// flag, the device's UI scale, the renderer's frame floats). It asks for a size
// once it has been seen, clamped, for 10 swaps in a row; 0x0 is ignored, a
// return to the current frame does nothing, a failed size is asked for again
// only after another size.
//
// Under a preset the game draws its own table frame for the reported mode
// (not 4:3 or wider: 1920x960; wider than 1280: 1920x1080; else 1280x720), with
// the reported mode's widescreen rule and the 720-high design. A switch between
// output and a preset resizes at once, to the table frame or to the output (or
// the forced dev size), and drops a pending window settle; under a preset the
// window changes nothing. The latch takes the new mode only once the frame is
// made for it; a switch that failed keeps the old mode until the next request.
#include "screen_layout_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

namespace {

using futari::screen_layout::CanvasBands;
using futari::screen_layout::CanvasFill;
using futari::screen_layout::CanvasSprite;
using futari::screen_layout::ArmTitle;
using futari::screen_layout::ClampFrame;
using futari::screen_layout::ClassifyPicture;
using futari::screen_layout::CoverPicture;
using futari::screen_layout::FrameApplied;
using futari::screen_layout::FrameMode;
using futari::screen_layout::FrameReady;
using futari::screen_layout::FrameRect;
using futari::screen_layout::FrameSize;
using futari::screen_layout::FrameTarget;
using futari::screen_layout::HintAnchor;
using futari::screen_layout::HintPoint;
using futari::screen_layout::IsFullCanvasPicture;
using futari::screen_layout::IsTitlePicture;
using futari::screen_layout::IsWidescreen;
using futari::screen_layout::kCanvasBlack;
using futari::screen_layout::kLogoArmed;
using futari::screen_layout::kTitleArmed;
using futari::screen_layout::kNarrowUiCentring;
using futari::screen_layout::kUi4x3ContentWidth;
using futari::screen_layout::kResizeSettleFrames;
using futari::screen_layout::kUiDesignHeight;
using futari::screen_layout::ParseFrameSize;
using futari::screen_layout::PictureDraw;
using futari::screen_layout::PictureFill;
using futari::screen_layout::PlanFrame;
using futari::screen_layout::PresetTargets;
using futari::screen_layout::ResizeSettle;
using futari::screen_layout::ResizeTargets;
using futari::screen_layout::ResizeValues;
using futari::screen_layout::SettleResize;
using futari::screen_layout::TableFrame;
using futari::screen_layout::UiDesignHeight;
using futari::screen_layout::UiScale;
using futari::screen_layout::WallpaperCover;
using futari::screen_layout::WallpaperQuad;
using futari::screen_layout::WallpaperSlide;

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

std::string Text(WallpaperQuad q) {
  return "(" + std::to_string(q.x0) + ", " + std::to_string(q.y0) + ") .. (" + std::to_string(q.x1) + ", " +
         std::to_string(q.y1) + ")";
}

void WallpaperCoverAt16By9IsTheGamesQuad() {
  for (const float d : {0.0f, 128.0f}) {
    const WallpaperQuad got = WallpaperCover(1920, 1080, d);
    const WallpaperQuad want{-d, -d * 9 / 16, 1920 + d, 1080 + d * 9 / 16};
    Check(got == want, d == 0 ? "1920x1080 at d = 0 is the frame" : "1920x1080 at d = 128 is the game's wide quad",
          Text(got));
  }
}

void WallpaperCoverCoversEveryAspect() {
  const FrameSize sizes[] = {{1920, 1200}, {2560, 1080}, {2520, 1680}, {1080, 1920}, {640, 480}, {8192, 480}};
  for (const FrameSize size : sizes) {
    const float w = float(size.width);
    const float h = float(size.height);
    for (const float d : {0.0f, 128.0f}) {
      const WallpaperQuad q = WallpaperCover(w, h, d);
      const std::string at = Text(size) + " d=" + std::to_string(int(d)) + ": " + Text(q);
      Check(q.x0 <= 0 && q.y0 <= 0 && q.x1 >= w && q.y1 >= h, "the quad covers the frame", at);
      Check(std::fabs((q.x1 - q.x0) / (q.y1 - q.y0) - 16.0f / 9.0f) < 1e-4f, "the quad is 16:9", at);
      Check(std::fabs((q.x0 + q.x1) - w) < 1e-2f && std::fabs((q.y0 + q.y1) - h) < 1e-2f, "the quad is centred", at);
      if (d == 0)
        Check(q.x0 == 0 || q.y0 == 0, "at d = 0 the quad is the smallest cover (one side fits)", at);
    }
  }
}

void WallpaperSlideFromTheGamesX0() {
  Check(WallpaperSlide(0, true) == 0, "wide branch, x0 = 0: d = 0", std::to_string(WallpaperSlide(0, true)));
  Check(WallpaperSlide(-128, true) == 128, "wide branch, x0 = -128: d = 128",
        std::to_string(WallpaperSlide(-128, true)));
  Check(WallpaperSlide(320, false) == 0, "4:3 branch, x0 = 320: d = 0", std::to_string(WallpaperSlide(320, false)));
  Check(WallpaperSlide(192, false) == 128, "4:3 branch, x0 = 192: d = 128",
        std::to_string(WallpaperSlide(192, false)));
}

void CheckCoverHolds(float w, float h, float slide, const char* what) {
  const WallpaperQuad q = WallpaperCover(w, h, slide);
  const std::string at = std::string(what) + ": " + Text(q);
  Check(std::isfinite(q.x0) && std::isfinite(q.y0) && std::isfinite(q.x1) && std::isfinite(q.y1),
        "the quad is finite", at);
  Check(q.x0 <= 0 && q.y0 <= 0 && q.x1 >= w && q.y1 >= h, "the quad covers the frame", at);
  Check(std::fabs((q.x1 - q.x0) / (q.y1 - q.y0) - 16.0f / 9.0f) < 1e-4f, "the quad is 16:9", at);
}

void WallpaperCoverWithASlideOutsideItsRange() {
  // A guest x0 past the game's own range gives a negative slide.
  CheckCoverHolds(1920, 1080, WallpaperSlide(320, true), "wide branch, x0 = 320");
  CheckCoverHolds(1920, 1080, WallpaperSlide(640, false), "4:3 branch, x0 = 640");
  CheckCoverHolds(1080, 1920, WallpaperSlide(1, true), "portrait, wide branch, x0 = 1");
  for (const float d : {-1.0f, -320.0f})
    CheckCoverHolds(1920, 1080, d, "negative slide");
  Check(WallpaperCover(1920, 1080, -320) == WallpaperCover(1920, 1080, 0), "a negative slide is slide 0",
        Text(WallpaperCover(1920, 1080, -320)));
  // x0 read as a guest float can be NaN or infinite.
  CheckCoverHolds(1920, 1080, std::nanf(""), "NaN slide");
  CheckCoverHolds(1920, 1080, INFINITY, "+inf slide");
  CheckCoverHolds(1920, 1080, -INFINITY, "-inf slide");
  CheckCoverHolds(1920, 1080, WallpaperSlide(std::nanf(""), false), "4:3 branch, x0 = NaN");
  Check(WallpaperCover(1920, 1080, std::nanf("")) == WallpaperCover(1920, 1080, 0), "a NaN slide is slide 0",
        Text(WallpaperCover(1920, 1080, std::nanf(""))));
  Check(WallpaperCover(1920, 1080, INFINITY) == WallpaperCover(1920, 1080, 128), "a slide above 128 is 128",
        Text(WallpaperCover(1920, 1080, INFINITY)));
}

std::string Text(FrameSize size, float value) { return Text(size) + ": " + std::to_string(value); }

void UiCanvasNarrowShowsThe4x3ContentAcrossTheWidth() {
  const FrameSize sizes[] = {{1080, 1920}, {720, 1280}, {1280, 1024}};
  for (const FrameSize size : sizes) {
    const float shown = float(size.width) / UiScale(size.width, size.height);
    Check(std::fabs(shown - 960) < 1e-2f, "a frame narrower than 4:3 shows 960 design units across its width",
          Text(size, shown));
  }
  Check(std::fabs(UiDesignHeight(1080, 1920) - 1706.6667f) < 1e-2f, "1080x1920: design height 960*H/W",
        Text(FrameSize{1080, 1920}, UiDesignHeight(1080, 1920)));
  Check(std::fabs(UiScale(1080, 1920) - 1.125f) < 1e-5f, "1080x1920: scale W/960",
        Text(FrameSize{1080, 1920}, UiScale(1080, 1920)));
  Check(std::fabs(UiDesignHeight(1280, 1024) - 768) < 1e-3f, "1280x1024 (5:4): design height 768",
        Text(FrameSize{1280, 1024}, UiDesignHeight(1280, 1024)));
}

void UiCanvasUnchangedAt4x3AndWider() {
  const FrameSize sizes[] = {{640, 480}, {1920, 1080}, {1920, 1200}, {2520, 1680}, {3840, 2160}};
  for (const FrameSize size : sizes) {
    const float height = UiDesignHeight(size.width, size.height);
    Check(height == 720, "4:3 or wider keeps the game's design height 720", Text(size, height));
    const float scale = UiScale(size.width, size.height);
    Check(std::fabs(scale - float(size.height) / 720) < 1e-6f, "4:3 or wider keeps the scale H/720",
          Text(size, scale));
  }
}

void UiCanvasContinuousAt4x3() {
  CheckWidescreen(640, 481, false, "640x481 (just below 4:3) takes the narrow canvas");
  const float below = UiDesignHeight(639, 480);
  Check(std::fabs(below - 720) < 1.5f, "639x480 (just below 4:3): design height about 720", Text({639, 480}, below));
  const float near = UiDesignHeight(1023, 768);
  Check(std::fabs(near - 720) < 1.0f, "1023x768 (just below 4:3): design height about 720", Text({1023, 768}, near));
}

void UiCentringIsTheConsoles4x3Value() {
  Check(kNarrowUiCentring == kUiDesignHeight * 16 / 9 - kUi4x3ContentWidth,
        "the narrow centring offset is the 16:9 design width less the 4:3 content width",
        std::to_string(kNarrowUiCentring));
}

void CheckHint(FrameRect rect, HintPoint display, FrameSize output, HintPoint want, const char* what) {
  const HintPoint got = HintAnchor(rect, display, output);
  Check(std::fabs(got.x - want.x) < 1e-3f && std::fabs(got.y - want.y) < 1e-3f, what,
        std::to_string(got.x) + ", " + std::to_string(got.y));
}

void HintAtTheFramesCorner() {
  CheckHint({0, 0, 1920, 1080}, {1920, 1080}, {1920, 1080}, {1904, 1064},
            "frame = window: the window corner less 16");
  CheckHint({320, 0, 1920, 1080}, {2560, 1080}, {2560, 1080}, {2224, 1064},
            "pillarbox 1920x1080 in 2560x1080: 16 inside the right bar's edge");
  CheckHint({0, 60, 1920, 1080}, {1920, 1200}, {1920, 1200}, {1904, 1124},
            "letterbox 1920x1080 in 1920x1200: 16 above the bottom bar");
}

void HintInDisplayUnits() {
  CheckHint({0, 0, 2400, 1350}, {1600, 900}, {2400, 1350}, {1584, 884},
            "DPI 1.5, frame = window: display corner less 16");
  CheckHint({300, 0, 1800, 1350}, {1600, 900}, {2400, 1350}, {1384, 884},
            "DPI 1.5, pillarbox: frame edge 2100 px = 1400 display units, less 16");
}

void HintWithoutAFrameAtTheWindowCorner() {
  CheckHint({}, {1920, 1080}, {1920, 1080}, {1904, 1064}, "no frame: the window corner less 16");
  CheckHint({100, 100, 0, 500}, {1600, 900}, {2400, 1350}, {1584, 884}, "zero-width frame: the window corner");
  CheckHint({100, 100, 500, 0}, {1600, 900}, {2400, 1350}, {1584, 884}, "zero-height frame: the window corner");
  CheckHint({0, 0, 1920, 1080}, {1920, 1080}, {0, 0}, {1904, 1064}, "no output size: the window corner");
}

// The CAVE logo as the game draws it: texels (1, 1) 1280x720 of its atlas over the whole canvas, faded to 0x53.
CanvasSprite Logo() {
  return {{-640, 640, 640, -640}, {-360, -360, 360, 360}, 1, 1, 1280, 720,
          {0xFF535353, 0xFF535353, 0xFF535353, 0xFF535353}};
}

std::string Text(const CanvasSprite& s) {
  std::string out;
  for (int i = 0; i < 4; ++i) {
    out += "(" + std::to_string(s.x[i]) + ", " + std::to_string(s.y[i]) + ") ";
  }
  return out + "texels " + std::to_string(s.tx) + ", " + std::to_string(s.ty) + " " + std::to_string(s.tw) + "x" +
         std::to_string(s.th);
}

void FullCanvasPictures() {
  Check(IsFullCanvasPicture(Logo()), "the logo is a full-canvas picture", Text(Logo()));
  CanvasSprite m2 = Logo();
  m2.ty = 723;
  Check(IsFullCanvasPicture(m2), "the M2 logo (second image of the atlas) is one", Text(m2));
  CanvasSprite moved = Logo();
  moved.x[1] = moved.x[2] = 639;
  Check(!IsFullCanvasPicture(moved), "a quad 1 unit short of the right edge is not", Text(moved));
  CanvasSprite cropped = Logo();
  cropped.tw = 1279;
  Check(!IsFullCanvasPicture(cropped), "an image 1279 texels wide is not", Text(cropped));
  CanvasSprite blended = Logo();
  blended.colour[2] = 0xFE535353;
  Check(!IsFullCanvasPicture(blended), "a corner below full alpha is not", Text(blended));
  const CanvasSprite frame{{-392, 392, 392, -392}, {-33, -33, 33, 33}, 32, 32, 1, 1, {~0u, ~0u, ~0u, ~0u}};
  Check(!IsFullCanvasPicture(frame), "a menu window piece is not", Text(frame));
}

void CanvasBandsTileTheOutside() {
  const float r = 1000;
  const std::array<CanvasSprite, 8> b = CanvasBands(Logo(), CanvasFill::kEdge, r);
  // left, right, top, bottom, then the corners top-left, top-right, bottom-right, bottom-left
  const float want[8][4] = {{-1640, -360, -640, 360}, {640, -360, 1640, 360},   {-640, -1360, 640, -360},
                            {-640, 360, 640, 1360},   {-1640, -1360, -640, -360}, {640, -1360, 1640, -360},
                            {640, 360, 1640, 1360},   {-1640, 360, -640, 1360}};
  double area = 0;
  for (int i = 0; i < 8; ++i) {
    const CanvasSprite& s = b[i];
    const bool corners = s.x[0] == want[i][0] && s.y[0] == want[i][1] && s.x[1] == want[i][2] &&
                         s.y[1] == want[i][1] && s.x[2] == want[i][2] && s.y[2] == want[i][3] &&
                         s.x[3] == want[i][0] && s.y[3] == want[i][3];
    Check(corners, ("band " + std::to_string(i) + " has its corners in the game's order").c_str(), Text(s));
    area += double(s.x[2] - s.x[0]) * (s.y[2] - s.y[0]);
  }
  Check(area == (1280.0 + 2 * r) * (720.0 + 2 * r) - 1280.0 * 720, "the bands' area is the outside, no overlap",
        std::to_string(area));
}

void CanvasBandsRepeatTheEdgeTexels() {
  CanvasSprite m2 = Logo();
  m2.ty = 723;
  const std::array<CanvasSprite, 8> b = CanvasBands(m2, CanvasFill::kEdge, 1000);
  // texel x, y, width, height per band: the edge column or row's centre, the image's span along the edge
  const float want[8][4] = {{1.5f, 723, 0, 720},       {1280.5f, 723, 0, 720},     {1, 723.5f, 1280, 0},
                            {1, 1442.5f, 1280, 0},     {1.5f, 723.5f, 0, 0},       {1280.5f, 723.5f, 0, 0},
                            {1280.5f, 1442.5f, 0, 0},  {1.5f, 1442.5f, 0, 0}};
  for (int i = 0; i < 8; ++i) {
    const CanvasSprite& s = b[i];
    Check(s.tx == want[i][0] && s.ty == want[i][1] && s.tw == want[i][2] && s.th == want[i][3],
          ("band " + std::to_string(i) + " samples the edge texels").c_str(), Text(s));
  }
}

void CanvasBandColours() {
  CanvasSprite gradient = Logo();
  gradient.colour[0] = 0xFF000001;  // top-left
  gradient.colour[1] = 0xFF000002;  // top-right
  gradient.colour[2] = 0xFF000003;  // bottom-right
  gradient.colour[3] = 0xFF000004;  // bottom-left
  const std::array<CanvasSprite, 8> edge = CanvasBands(gradient, CanvasFill::kEdge, 1000);
  const uint32_t want[8][4] = {{1, 1, 4, 4}, {2, 2, 3, 3}, {1, 2, 2, 1}, {4, 3, 3, 4},
                               {1, 1, 1, 1}, {2, 2, 2, 2}, {3, 3, 3, 3}, {4, 4, 4, 4}};
  for (int i = 0; i < 8; ++i) {
    bool same = true;
    for (int k = 0; k < 4; ++k) {
      same = same && edge[i].colour[k] == (0xFF000000 | want[i][k]);
    }
    Check(same, ("edge band " + std::to_string(i) + " takes the nearest corner colours").c_str(), Text(edge[i]));
  }
  const std::array<CanvasSprite, 8> black = CanvasBands(gradient, CanvasFill::kBlack, 1000);
  bool all_black = true;
  for (const CanvasSprite& s : black) {
    for (uint32_t c : s.colour) {
      all_black = all_black && c == kCanvasBlack;
    }
  }
  Check(all_black, "black bands are opaque black", Text(black[0]));
}

void LogoThenTitleFills() {
  uint32_t logo = 0;
  Check(PictureFill(logo, 0x40A1FE60) == CanvasFill::kBlack, "no logo motion loaded: black", std::to_string(logo));
  logo = kLogoArmed;
  Check(PictureFill(logo, 0x40A1FE60) == CanvasFill::kEdge, "the first picture after the logo loads: edge",
        std::to_string(logo));
  Check(PictureFill(logo, 0x40A1FE60) == CanvasFill::kEdge, "the logo's texture again (M2): edge",
        std::to_string(logo));
  Check(PictureFill(logo, 0x400EA640) == CanvasFill::kBlack, "the title's texture: black", std::to_string(logo));
  Check(logo == 0, "the title ends the logo", std::to_string(logo));
  Check(PictureFill(logo, 0x40A1FE60) == CanvasFill::kBlack, "the logo's address reused later: black",
        std::to_string(logo));
}

// The title's background as the game draws it: texels (1, 1) 1280x720 of the title atlas over the whole canvas.
CanvasSprite TitleBackground() {
  return {{-640, 640, 640, -640}, {-360, -360, 360, 360}, 1, 1, 1280, 720,
          {0xFFF9F9F9, 0xFFF9F9F9, 0xFFF9F9F9, 0xFFF9F9F9}};
}

bool SameTexelsAndColours(const CanvasSprite& a, const CanvasSprite& b) {
  bool same = a.tx == b.tx && a.ty == b.ty && a.tw == b.tw && a.th == b.th;
  for (int i = 0; i < 4; ++i) {
    same = same && a.colour[i] == b.colour[i];
  }
  return same;
}

void CoverPictureAt16By9IsTheGamesSprite() {
  const FrameSize sizes[] = {{1280, 720}, {1920, 1080}, {3840, 2160}};
  for (const FrameSize size : sizes) {
    const CanvasSprite got = CoverPicture(TitleBackground(), size.width, size.height);
    bool same = SameTexelsAndColours(got, TitleBackground());
    for (int i = 0; i < 4; ++i) {
      same = same && got.x[i] == TitleBackground().x[i] && got.y[i] == TitleBackground().y[i];
    }
    Check(same, "at 16:9 the title's background is the game's sprite", Text(size) + ": " + Text(got));
  }
}

void CoverPictureCoversEveryAspect() {
  const FrameSize sizes[] = {{2560, 1080}, {1920, 1200}, {2520, 1680}, {640, 480}, {1080, 1920}, {8192, 480}};
  for (const FrameSize size : sizes) {
    const CanvasSprite got = CoverPicture(TitleBackground(), size.width, size.height);
    const std::string at = Text(size) + ": " + Text(got);
    // The frame in design units: H / scale high, W / scale wide, centred on 0.
    const float scale = UiScale(size.width, size.height);
    const float half_w = float(size.width) / scale / 2;
    const float half_h = float(size.height) / scale / 2;
    const float x0 = got.x[0], y0 = got.y[0], x1 = got.x[2], y1 = got.y[2];
    const bool corners = got.x[3] == x0 && got.x[1] == x1 && got.y[1] == y0 && got.y[3] == y1;
    Check(corners, "the cover keeps the game's corner order", at);
    Check(x0 <= -half_w + 1e-2f && x1 >= half_w - 1e-2f && y0 <= -half_h + 1e-2f && y1 >= half_h - 1e-2f,
          "the cover covers the frame", at);
    Check(std::fabs(x0 + x1) < 1e-3f && std::fabs(y0 + y1) < 1e-3f, "the cover is centred", at);
    Check(std::fabs((x1 - x0) / (y1 - y0) - 16.0f / 9.0f) < 1e-4f, "the cover keeps 16:9", at);
    Check(std::fabs(x1 - half_w) < 1e-2f || std::fabs(y1 - half_h) < 1e-2f || x1 == 640,
          "the cover is the smallest (one side fits, or the game's canvas)", at);
    Check(SameTexelsAndColours(got, TitleBackground()), "the cover keeps the texels and colours", at);
  }
  const CanvasSprite wide = CoverPicture(TitleBackground(), 2560, 1080);
  Check(std::fabs(wide.x[2] - 2560.0f / 3) < 1e-2f && std::fabs(wide.y[2] - 480) < 1e-2f,
        "2560x1080: the cover spans 853.3 x 480 units each side", Text(wide));
  const CanvasSprite tall = CoverPicture(TitleBackground(), 1080, 1920);
  Check(std::fabs(tall.y[2] - 2560.0f / 3) < 1e-2f, "1080x1920: the cover spans 853.3 units up and down", Text(tall));
  const CanvasSprite square4x3 = CoverPicture(TitleBackground(), 640, 480);
  Check(square4x3.x[2] == 640 && square4x3.y[2] == 360, "4:3: the game's canvas already covers", Text(square4x3));
}

void TitlePictureAfterTheTitleLoads() {
  uint32_t title = 0;
  Check(!IsTitlePicture(title, 0x400EB210), "no title loaded: not the title", std::to_string(title));
  title = kTitleArmed;
  Check(IsTitlePicture(title, 0x400EB210), "the first picture after the title loads is the title's",
        std::to_string(title));
  Check(IsTitlePicture(title, 0x400EB210), "the title's texture again", std::to_string(title));
  Check(!IsTitlePicture(title, 0x400E7C60), "another picture is not", std::to_string(title));
  Check(title == 0, "another picture ends the title", std::to_string(title));
  Check(!IsTitlePicture(title, 0x400EB210), "after another picture the old address is not the title",
        std::to_string(title));
  title = kTitleArmed;
  Check(IsTitlePicture(title, 0x400F29F0), "the title loaded again: its new texture", std::to_string(title));
}

const char* DrawName(PictureDraw draw) {
  return draw == PictureDraw::kCover ? "cover" : draw == PictureDraw::kEdgeBands ? "edge bands" : "black bands";
}

// Both words together, every state each can be in (none, armed, bound to its texture) against the logo's texture, the
// title's and a third one: the logo's texture first (edge), then the title's (cover), black for the rest.
void PictureDrawFromBothWords() {
  constexpr uint32_t L = 0x40A1FE60, T = 0x400EB210, X = 0x400E7C60;
  const uint32_t states_logo[] = {0, kLogoArmed, L};
  const uint32_t states_title[] = {0, kTitleArmed, T};
  for (const uint32_t logo_in : states_logo) {
    for (const uint32_t title_in : states_title) {
      for (const uint32_t texture : {L, T, X}) {
        uint32_t logo = logo_in, title = title_in;
        const PictureDraw got = ClassifyPicture(logo, title, texture);
        PictureDraw want = PictureDraw::kBlackBands;
        uint32_t want_logo = 0, want_title = title_in;
        if (logo_in == kLogoArmed || (logo_in == L && texture == L)) {
          want = PictureDraw::kEdgeBands;
          want_logo = texture;
        } else if (title_in == kTitleArmed || (title_in == T && texture == T)) {
          want = PictureDraw::kCover;
          want_title = texture;
        } else if (title_in == T) {
          want_title = 0;
        }
        const std::string at = "logo " + std::to_string(logo_in) + ", title " + std::to_string(title_in) +
                               ", texture " + std::to_string(texture) + ": " + DrawName(got) + ", logo " +
                               std::to_string(logo) + ", title " + std::to_string(title);
        Check(got == want && logo == want_logo && title == want_title, "the draw from both words", at);
      }
    }
  }
}

// The title's load ends a logo that never drew a picture, so the title's background is never taken for the logo
// (also when it gets the logo's freed address). A logo that did draw keeps its texture: its last frames (M2) still
// draw after the title loads, up to the title's own first picture.
void TitleLoadEndsTheLogosArming() {
  constexpr uint32_t L = 0x40A1FE60, T = 0x400EB210;
  uint32_t logo = kLogoArmed, title = 0;
  ArmTitle(logo, title);
  Check(logo == 0 && title == kTitleArmed, "the title loads after a logo that drew nothing: the logo ends",
        std::to_string(logo));
  Check(ClassifyPicture(logo, title, T) == PictureDraw::kCover, "no logo picture after the logo loaded: the title covers",
        std::to_string(title));
  logo = kLogoArmed;
  title = 0;
  ArmTitle(logo, title);
  Check(ClassifyPicture(logo, title, L) == PictureDraw::kCover,
        "the title's background at the never-drawn logo's address covers", std::to_string(title));
  logo = kLogoArmed;
  title = 0;
  Check(ClassifyPicture(logo, title, L) == PictureDraw::kEdgeBands, "the logo's first picture", std::to_string(logo));
  ArmTitle(logo, title);
  Check(logo == L, "the title loads while the logo draws: the logo keeps its texture", std::to_string(logo));
  Check(ClassifyPicture(logo, title, L) == PictureDraw::kEdgeBands, "the logo's last frames after the title loads: edge",
        std::to_string(logo));
  Check(ClassifyPicture(logo, title, T) == PictureDraw::kCover, "then the title's own picture covers",
        std::to_string(title));
  Check(logo == 0 && title == T, "and ends the logo", std::to_string(logo));
  Check(ClassifyPicture(logo, title, L) == PictureDraw::kBlackBands, "the logo's address after that: black",
        std::to_string(logo));
}

bool Near(float got, double want) { return std::fabs(double(got) - want) < 1e-5 * std::max(1.0, std::fabs(want)); }

std::string Text(const ResizeValues& v) {
  return std::to_string(v.width) + "x" + std::to_string(v.height) + " wide " + std::to_string(v.widescreen) +
         " design " + std::to_string(v.ui_design_height) + " floats " + std::to_string(v.screen_width) + "x" +
         std::to_string(v.screen_height) + " flag " + std::to_string(v.widescreen_flag) + " scale " +
         std::to_string(v.ui_scale) + " renderer " + std::to_string(v.renderer_width) + "x" +
         std::to_string(v.renderer_height);
}

void CheckTargets(uint32_t w, uint32_t h, bool wide, double design, double scale) {
  const ResizeValues v = ResizeTargets(w, h);
  const std::string at = Text(v);
  Check(v.width == w && v.height == h, "the block's frame is the size", at);
  Check(v.widescreen == (wide ? 1 : 0) && v.widescreen_flag == (wide ? 1u : 0u),
        wide ? "4:3 or wider: the block's byte and the game's flag are 1" : "narrower than 4:3: both are 0", at);
  Check(Near(v.ui_design_height, design), "the UI design height", at);
  Check(v.screen_width == float(w) && v.screen_height == float(h), "the screen floats are the size", at);
  Check(Near(v.ui_scale, scale), "the device's UI scale", at);
  Check(v.ui_scale == float(float(h) / v.ui_design_height), "the device's scale is the game's H / design height", at);
  Check(v.renderer_width == float(w) && v.renderer_height == float(h), "the renderer's frame floats are the size", at);
}

// Every value a live resize writes, at the listed sizes.
void ResizeTargetsAtEachSize() {
  CheckTargets(1920, 1080, true, 720, 1.5);
  CheckTargets(1600, 900, true, 720, 1.25);
  CheckTargets(2560, 1080, true, 720, 1.5);
  CheckTargets(1080, 1920, false, 960.0 * 1920 / 1080, 1080.0 / 960);
  CheckTargets(1280, 1024, false, 768, 1280.0 / 960);
}

// Across the 4:3 line, both ways: exactly 4:3 keeps the wide values, one pixel narrower takes the 4:3 branches, and
// the scale barely moves; going back gives the wide values again (the targets depend on the size alone).
void ResizeTargetsAcrossThe4x3Line() {
  CheckTargets(1440, 1080, true, 720, 1.5);
  CheckTargets(1439, 1080, false, 960.0 * 1080 / 1439, 1439.0 / 960);
  const ResizeValues wide = ResizeTargets(1440, 1080);
  const ResizeValues narrow = ResizeTargets(1439, 1080);
  Check(std::fabs(wide.ui_scale - narrow.ui_scale) < 0.002f, "the scale is continuous across 4:3", Text(narrow));
  CheckTargets(1440, 1080, true, 720, 1.5);
  CheckTargets(1920, 1440, true, 720, 2);
  CheckTargets(1920, 1441, false, 960.0 * 1441 / 1920, 2);
}

// Feeds w x h for n swaps; the resizes it asks for.
int Feed(ResizeSettle& s, uint32_t w, uint32_t h, int n, std::optional<FrameSize>* last = nullptr) {
  int fired = 0;
  for (int i = 0; i < n; ++i) {
    if (const std::optional<FrameSize> got = SettleResize(s, w, h)) {
      ++fired;
      if (last)
        *last = got;
    }
  }
  return fired;
}

void SettleADragStormSettlesOnce() {
  ResizeSettle s{{1280, 720}, {1280, 720}, 0};
  int fired = 0;
  for (uint32_t i = 0; i < 40; ++i)
    fired += Feed(s, 1300 + i * 7, 730 + i * 3, 1);
  Check(fired == 0, "a size that changes every swap never settles", std::to_string(fired));
  Check(Feed(s, 1600, 900, int(kResizeSettleFrames) - 1) == 0, "9 swaps at the new size: not yet", Text(s.pending));
  std::optional<FrameSize> got;
  Check(Feed(s, 1600, 900, 1, &got) == 1 && got == FrameSize{1600, 900}, "the 10th swap asks for 1600x900", Text(got));
  Check(Feed(s, 1600, 900, 50) == 0, "and only once", Text(s.pending));
  s.frame = {1600, 900};
  Check(Feed(s, 1600, 900, 50) == 0, "once applied, the same size does nothing", Text(s.frame));
}

void SettleReturnToTheOldSizeDoesNothing() {
  ResizeSettle s{{1280, 720}, {1280, 720}, 0};
  Check(Feed(s, 1600, 900, 5) == 0 && Feed(s, 1280, 720, 50) == 0, "back to the frame within the settle: nothing",
        Text(s.pending));
  Check(Feed(s, 1600, 900, int(kResizeSettleFrames) - 1) == 0, "the count starts again after a return",
        Text(s.pending));
}

void SettleIgnoresZero() {
  ResizeSettle s{{1280, 720}, {1280, 720}, 0};
  Check(Feed(s, 0, 0, 50) == 0 && s.frame == FrameSize{1280, 720}, "0x0 asks for nothing and keeps the frame",
        Text(s.frame));
  Check(Feed(s, 0, 900, 5) + Feed(s, 1600, 0, 5) == 0, "a zero side is ignored too", Text(s.pending));
  std::optional<FrameSize> got;
  const int fired = Feed(s, 1600, 900, 5) + Feed(s, 0, 0, 30) + Feed(s, 1600, 900, 5, &got);
  Check(fired == 1 && got == FrameSize{1600, 900}, "0x0 (minimised) between swaps neither counts nor resets",
        Text(got));
}

void SettleCountsTheClampedSize() {
  ResizeSettle s{{640, 480}, {640, 480}, 0};
  Check(Feed(s, 300, 200, 50) == 0, "a size clamped to the frame does nothing", Text(s.pending));
  s = {{1280, 720}, {1280, 720}, 0};
  std::optional<FrameSize> got;
  const int fired = Feed(s, 300, 200, 5) + Feed(s, 320, 240, 5, &got);
  Check(fired == 1 && got == FrameSize{640, 480}, "two sizes clamped to 640x480 count as one", Text(got));
  Check(Feed(s, 9000, 1080, int(kResizeSettleFrames), &got) == 1 && got == FrameSize{8192, 1080},
        "a wide size settles at its clamped value", Text(got));
}

// A resize that failed (nothing applied) is asked for again only after the size changes.
void SettleRetriesOnlyOnTheNextChange() {
  ResizeSettle s{{1280, 720}, {1280, 720}, 0};
  Check(Feed(s, 1600, 900, int(kResizeSettleFrames)) == 1, "1600x900 settles", Text(s.pending));
  Check(Feed(s, 1600, 900, 50) == 0, "not applied: the same size is not asked for again", Text(s.pending));
  Check(Feed(s, 1700, 900, int(kResizeSettleFrames)) == 1, "a new size settles", Text(s.pending));
  Check(Feed(s, 1600, 900, int(kResizeSettleFrames)) == 1, "and the failed size again after it", Text(s.pending));
}

void CheckTable(uint32_t w, uint32_t h, FrameSize want, const char* what) {
  const FrameSize got = TableFrame(w, h);
  Check(got == want, what, Text(got));
}

// The game's own table (sub_8216EBE8) for reported modes.
void TableFrameForReportedModes() {
  CheckTable(1280, 720, {1280, 720}, "1280x720 reported: 1280x720");
  CheckTable(1600, 900, {1920, 1080}, "1600x900 reported: wider than 1280, 1920x1080");
  CheckTable(1920, 1080, {1920, 1080}, "1920x1080 reported: 1920x1080");
  // The reported widescreen bit is "4:3 or wider" (VdQueryVideoMode), so exactly 4:3 is not the 1920x960 entry.
  CheckTable(640, 480, {1280, 720}, "640x480 reported: 4:3 counts as wide, not wider than 1280: 1280x720");
  CheckTable(1920, 1440, {1920, 1080}, "1920x1440 reported: 4:3 and wider than 1280: 1920x1080");
  CheckTable(1280, 1024, {1920, 960}, "1280x1024 reported: narrower than 4:3, 1920x960");
  CheckTable(1080, 1920, {1920, 960}, "1080x1920 reported: portrait, 1920x960");
  CheckTable(2560, 1440, {1920, 1080}, "2560x1440 reported: 1920x1080");
}

// The values a preset's frame gets: the table frame, the reported mode's widescreen rule (as the game's +73 and
// flag), the 720-high design and H/720.
void CheckPreset(uint32_t w, uint32_t h, FrameSize frame, bool wide) {
  const ResizeValues v = PresetTargets(w, h);
  const std::string at = std::to_string(w) + "x" + std::to_string(h) + ": " + Text(v);
  Check(v.width == frame.width && v.height == frame.height, "the block's frame is the table's", at);
  Check(v.widescreen == (wide ? 1 : 0) && v.widescreen_flag == (wide ? 1u : 0u),
        "the byte and the flag follow the reported mode", at);
  Check(v.ui_design_height == kUiDesignHeight, "the design height is 720", at);
  Check(v.ui_scale == float(float(frame.height) / kUiDesignHeight), "the device's scale is H / 720", at);
  Check(v.screen_width == float(frame.width) && v.screen_height == float(frame.height) &&
            v.renderer_width == float(frame.width) && v.renderer_height == float(frame.height),
        "the floats are the table frame", at);
}

void PresetTargetsAreTheGamesOwn() {
  CheckPreset(1280, 720, {1280, 720}, true);
  CheckPreset(1920, 1080, {1920, 1080}, true);
  CheckPreset(1600, 900, {1920, 1080}, true);
  // 1920x960 is wider than 4:3, but the game's byte follows the 4:3 reported mode.
  CheckPreset(1280, 1024, {1920, 960}, false);
  CheckPreset(1080, 1920, {1920, 960}, false);
  // At a table size with a widescreen reported mode, the same values as a live resize to it.
  const ResizeValues preset = PresetTargets(1920, 1080);
  const ResizeValues resize = ResizeTargets(1920, 1080);
  Check(preset.widescreen == resize.widescreen && preset.ui_design_height == resize.ui_design_height &&
            preset.ui_scale == resize.ui_scale,
        "1080p's values are a 1920x1080 resize's", Text(preset));
}

std::string Text(std::optional<FrameTarget> t) {
  return t ? Text(t->frame) + (t->output ? " output" : " preset") : std::string("none");
}

constexpr FrameSize kReported{1600, 900};

// Output, a 2560x1440 frame (booted there), the window at 2560x1440.
FrameMode OutputAt2560() { return {{{2560, 1440}, {2560, 1440}, 0}, true, true}; }

// output -> preset: the table frame at once, no settle; the gate opens only once it is applied.
void SwitchToAPresetAtOnce() {
  FrameMode m = OutputAt2560();
  Check(!FrameReady(m.output, false), "before the switch the frame is not a preset's", "");
  const std::optional<FrameTarget> t = PlanFrame(m, false, {2560, 1440}, std::nullopt, kReported);
  Check(t && t->frame == FrameSize{1920, 1080} && !t->output, "the first swap asks for the table frame", Text(t));
  Check(!FrameReady(m.output, false), "the gate stays closed until the resize is applied", "");
  FrameApplied(m, *t);
  Check(FrameReady(m.output, false) && !FrameReady(m.output, true), "applied: the gate opens for the preset", "");
  Check(!PlanFrame(m, false, {2560, 1440}, std::nullopt, kReported), "the next swap asks for nothing",
        Text(m.settle.frame));
}

// A switch drops a window size that was settling.
void SwitchDropsAPendingSettle() {
  FrameMode m = OutputAt2560();
  for (uint32_t i = 0; i + 1 < kResizeSettleFrames; ++i)
    PlanFrame(m, true, {1600, 900}, std::nullopt, kReported);
  const std::optional<FrameTarget> t = PlanFrame(m, false, {1600, 900}, std::nullopt, kReported);
  Check(t && t->frame == FrameSize{1920, 1080}, "the switch is taken", Text(t));
  FrameApplied(m, *t);
  int asked = 0;
  for (int i = 0; i < 30; ++i)
    asked += PlanFrame(m, false, {1600, 900}, std::nullopt, kReported).has_value();
  Check(asked == 0 && m.settle.swaps == 0, "the pending 1600x900 never fires under the preset", Text(m.settle.pending));
}

// preset -> output: the output's size at once (or the forced dev size).
void SwitchToOutputAtOnce() {
  FrameMode m{{{1920, 1080}, {1920, 1080}, 0}, false, false};
  const std::optional<FrameTarget> t = PlanFrame(m, true, {2560, 1440}, std::nullopt, kReported);
  Check(t && t->frame == FrameSize{2560, 1440} && t->output, "the first swap asks for the output", Text(t));
  Check(!FrameReady(m.output, true), "not ready before it is applied", "");
  FrameApplied(m, *t);
  Check(FrameReady(m.output, true), "ready once applied", "");
  Check(!PlanFrame(m, true, {2560, 1440}, std::nullopt, kReported), "then nothing", Text(m.settle.frame));
  m = {{{1920, 1080}, {1920, 1080}, 0}, false, false};
  const std::optional<FrameTarget> forced = PlanFrame(m, true, {2560, 1440}, FrameSize{3840, 2160}, kReported);
  Check(forced && forced->frame == FrameSize{3840, 2160}, "a forced dev size wins", Text(forced));
  m = {{{1920, 1080}, {1920, 1080}, 0}, false, false};
  const std::optional<FrameTarget> small = PlanFrame(m, true, {300, 200}, std::nullopt, kReported);
  Check(small && small->frame == FrameSize{640, 480}, "the output is clamped", Text(small));
  m = {{{1920, 1080}, {1920, 1080}, 0}, false, false};
  const std::optional<FrameTarget> none = PlanFrame(m, true, {0, 0}, std::nullopt, kReported);
  Check(none && none->frame == FrameSize{1920, 1080} && none->output, "no output size yet: the frame it has",
        Text(none));
}

// Under a preset the window's size changes nothing: the table frame stays and the present scales it.
void PresetIgnoresTheWindow() {
  FrameMode m{{{1920, 1080}, {1920, 1080}, 0}, false, false};
  int asked = 0;
  for (int i = 0; i < 50; ++i)
    asked += PlanFrame(m, false, {1600 + uint32_t(i % 3), 900}, std::nullopt, kReported).has_value();
  for (int i = 0; i < 50; ++i)
    asked += PlanFrame(m, false, {2560, 1440}, std::nullopt, kReported).has_value();
  Check(asked == 0 && m.settle.frame == FrameSize{1920, 1080}, "no resize under a preset", Text(m.settle.frame));
  Check(FrameReady(m.output, false), "preset -> preset needs nothing: ready at once", "");
}

// A failed switch: the old frame and mode stay, the gate stays closed, and it is not asked for every swap.
void FailedSwitchKeepsTheOldMode() {
  FrameMode m = OutputAt2560();
  Check(PlanFrame(m, false, {2560, 1440}, std::nullopt, kReported).has_value(), "the switch is asked for", "");
  Check(!FrameReady(m.output, false) && FrameReady(m.output, true), "failed: the gate holds the old mode", "");
  int asked = 0;
  for (int i = 0; i < 30; ++i)
    asked += PlanFrame(m, false, {2560, 1440}, std::nullopt, kReported).has_value();
  Check(asked == 0 && m.settle.frame == FrameSize{2560, 1440}, "not retried every swap", Text(m.settle.frame));
  Check(!PlanFrame(m, true, {2560, 1440}, std::nullopt, kReported), "back to the old mode: nothing to resize",
        Text(m.settle.frame));
  const std::optional<FrameTarget> again = PlanFrame(m, false, {2560, 1440}, std::nullopt, kReported);
  Check(again && again->frame == FrameSize{1920, 1080}, "a new request tries again", Text(again));
}

// Under output the window still settles as before; a fixed dev size turns that off.
void OutputStillSettles() {
  FrameMode m = OutputAt2560();
  int asked = 0;
  std::optional<FrameTarget> last;
  for (uint32_t i = 0; i < kResizeSettleFrames; ++i)
    if (const std::optional<FrameTarget> t = PlanFrame(m, true, {1600, 900}, std::nullopt, kReported)) {
      ++asked;
      last = t;
    }
  Check(asked == 1 && last->frame == FrameSize{1600, 900} && last->output, "a settled window size", Text(last));
  m = OutputAt2560();
  asked = 0;
  for (int i = 0; i < 30; ++i)
    asked += PlanFrame(m, true, {1600, 900}, FrameSize{2560, 1440}, kReported).has_value();
  Check(asked == 0, "a forced dev size does not follow the window", std::to_string(asked));
}

}  // namespace

int main() {
  ClampAtAndAroundTheBounds();
  ClampZeroAndHuge();
  ClampKeepsEveryListedSize();
  Widescreen();
  ParseDevFrameSize();
  ParsedSizesAreClamped();
  WallpaperCoverAt16By9IsTheGamesQuad();
  WallpaperCoverCoversEveryAspect();
  WallpaperSlideFromTheGamesX0();
  WallpaperCoverWithASlideOutsideItsRange();
  UiCanvasNarrowShowsThe4x3ContentAcrossTheWidth();
  UiCanvasUnchangedAt4x3AndWider();
  UiCanvasContinuousAt4x3();
  UiCentringIsTheConsoles4x3Value();
  HintAtTheFramesCorner();
  HintInDisplayUnits();
  HintWithoutAFrameAtTheWindowCorner();
  FullCanvasPictures();
  CanvasBandsTileTheOutside();
  CanvasBandsRepeatTheEdgeTexels();
  CanvasBandColours();
  LogoThenTitleFills();
  CoverPictureAt16By9IsTheGamesSprite();
  CoverPictureCoversEveryAspect();
  TitlePictureAfterTheTitleLoads();
  PictureDrawFromBothWords();
  TitleLoadEndsTheLogosArming();
  ResizeTargetsAtEachSize();
  ResizeTargetsAcrossThe4x3Line();
  SettleADragStormSettlesOnce();
  SettleReturnToTheOldSizeDoesNothing();
  SettleIgnoresZero();
  SettleCountsTheClampedSize();
  SettleRetriesOnlyOnTheNextChange();
  TableFrameForReportedModes();
  PresetTargetsAreTheGamesOwn();
  SwitchToAPresetAtOnce();
  SwitchDropsAPendingSettle();
  SwitchToOutputAtOnce();
  PresetIgnoresTheWindow();
  FailedSwitchKeepsTheOldMode();
  OutputStillSettles();
  std::printf("screen_layout_test: %d checks, %d failed\n", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
