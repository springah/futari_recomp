// The size rules of the game's output-sized frame (src/screen_layout.cpp), pure std so a test can run them without
// the SDK (tests/screen_layout_test.cpp).
#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string_view>

namespace futari::screen_layout {

inline constexpr uint32_t kMinFrameWidth = 640;
inline constexpr uint32_t kMinFrameHeight = 480;
inline constexpr uint32_t kMaxFrameSide = 8192;

struct FrameSize {
  uint32_t width = 0;
  uint32_t height = 0;
  bool operator==(const FrameSize&) const = default;
};

// The frame the game draws for an output of w x h: 640..8192 wide, 480..8192 high.
constexpr FrameSize ClampFrame(uint32_t w, uint32_t h) {
  return {std::clamp(w, kMinFrameWidth, kMaxFrameSide), std::clamp(h, kMinFrameHeight, kMaxFrameSide)};
}

// The game's widescreen rule: 4:3 or wider. Portrait and anything narrower take its 4:3 branches.
constexpr bool IsWidescreen(uint32_t w, uint32_t h) { return uint64_t(w) * 3 >= uint64_t(h) * 4; }

// The dev size "WxH" (x or X, spaces around the parts): two whole numbers above 0, unclamped; else nothing.
inline std::optional<FrameSize> ParseFrameSize(std::string_view text) {
  const auto number = [](std::string_view part) -> std::optional<uint32_t> {
    const size_t first = part.find_first_not_of(" \t");
    if (first == std::string_view::npos) {
      return std::nullopt;
    }
    part = part.substr(first, part.find_last_not_of(" \t") - first + 1);
    uint32_t value = 0;
    const auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), value);
    if (error != std::errc{} || end != part.data() + part.size() || value == 0) {
      return std::nullopt;
    }
    return value;
  };
  const size_t x = text.find_first_of("xX");
  if (x == std::string_view::npos) {
    return std::nullopt;
  }
  const std::optional<uint32_t> width = number(text.substr(0, x));
  const std::optional<uint32_t> height = number(text.substr(x + 1));
  if (!width || !height) {
    return std::nullopt;
  }
  return FrameSize{*width, *height};
}

// The menu wallpaper's quad in frame pixels (its art is 16:9, sampled u, v 0..1).
struct WallpaperQuad {
  float x0 = 0;
  float y0 = 0;
  float x1 = 0;
  float y1 = 0;
  bool operator==(const WallpaperQuad&) const = default;
};

// The game's 4:3 branch insets its quad by 320 px on each side (a 1920-wide frame's 4:3 window).
inline constexpr float kWallpaperInset4x3 = 320;

// The game's slide-in offset runs from 0 to 128 px.
inline constexpr float kWallpaperMaxSlide = 128;

// The wallpaper covering a w x h frame at every aspect: the smallest centred 16:9 quad that covers it, grown by the
// slide-in offset d as the game grows its wide quad (x by d, y by 9d/16 on each side). At 16:9 it is the game's quad.
// d comes from a guest float: NaN counts as 0 and the rest is clamped to 0..128, so the quad always covers.
constexpr WallpaperQuad WallpaperCover(float w, float h, float slide) {
  const double d = slide >= 0 ? std::min(double(slide), double(kWallpaperMaxSlide)) : 0.0;
  const double scale = std::max(double(w) / 16, double(h) / 9);
  const double half_w = (16 * scale + 2 * d) / 2;
  const double half_h = (9 * scale + 2 * d * 9 / 16) / 2;
  return {float(double(w) / 2 - half_w), float(double(h) / 2 - half_h), float(double(w) / 2 + half_w),
          float(double(h) / 2 + half_h)};
}

// The slide-in offset d from the game's own x0: -d on its wide branch, 320 - d on its 4:3 branch.
constexpr float WallpaperSlide(float x0, bool wide) { return wide ? -x0 : kWallpaperInset4x3 - x0; }

// The menu canvas: the game scales its UI by H / design height (720). Its 4:3 layout spans 960 design units.
inline constexpr float kUiDesignHeight = 720;
inline constexpr float kUi4x3ContentWidth = 960;
// The 4:3 layout's x offset on a console 4:3 mode: 720 * 16/9 - 720 * 4/3.
inline constexpr float kNarrowUiCentring = 320;

// The design height for a w x h frame: 720 at 4:3 or wider; below 4:3 the height that puts the 960-wide 4:3
// content across the frame width (960 * h / w, 720 at exactly 4:3).
constexpr float UiDesignHeight(uint32_t w, uint32_t h) {
  return IsWidescreen(w, h) ? kUiDesignHeight : float(double(kUi4x3ContentWidth) * h / w);
}

// The UI's scale in frame pixels per design unit: h / 720, or w / 960 below 4:3.
constexpr float UiScale(uint32_t w, uint32_t h) { return float(double(h) / UiDesignHeight(w, h)); }

// Where the present put the game's frame, in output pixels; zero size = none.
struct FrameRect {
  uint32_t x = 0;
  uint32_t y = 0;
  uint32_t width = 0;
  uint32_t height = 0;
};

// A point (or size) in the UI's display units.
struct HintPoint {
  float x = 0;
  float y = 0;
};

// The exit hint's distance from the frame's edges, in display units.
inline constexpr float kHintMargin = 16;

// The exit hint's bottom-right corner: kHintMargin inside the bottom-right corner of the frame `rect` (output
// pixels, scaled to the display's units, which differ under DPI scaling); the display's corner when there is no
// frame or no output size.
constexpr HintPoint HintAnchor(FrameRect rect, HintPoint display, FrameSize output) {
  if (rect.width == 0 || rect.height == 0 || output.width == 0 || output.height == 0) {
    return {display.x - kHintMargin, display.y - kHintMargin};
  }
  const double right = (double(rect.x) + rect.width) * display.x / output.width;
  const double bottom = (double(rect.y) + rect.height) * display.y / output.height;
  return {float(right) - kHintMargin, float(bottom) - kHintMargin};
}

// A sprite of the game's 2D draw: 4 corners in design units in the game's order (top-left, top-right,
// bottom-right, bottom-left), the texel rect it samples (x, y, width, height) and an ARGB colour per corner.
struct CanvasSprite {
  float x[4] = {};
  float y[4] = {};
  float tx = 0;
  float ty = 0;
  float tw = 0;
  float th = 0;
  uint32_t colour[4] = {};
};

// The design canvas the game's full-screen pictures cover: 1280x720 units centred on 0.
inline constexpr float kCanvasHalfWidth = 640;
inline constexpr float kCanvasHalfHeight = 360;
// How far the bands reach past the canvas, in design units (beyond any frame's edge).
inline constexpr float kCanvasBandReach = 16384;
inline constexpr uint32_t kCanvasBlack = 0xFF000000;

// A full-screen picture: an opaque 1280x720-texel image drawn over exactly the whole canvas (the boot logos and the
// title; no menu or gameplay sprite is).
constexpr bool IsFullCanvasPicture(const CanvasSprite& s) {
  const float hw = kCanvasHalfWidth;
  const float hh = kCanvasHalfHeight;
  const bool canvas = s.x[0] == -hw && s.y[0] == -hh && s.x[1] == hw && s.y[1] == -hh && s.x[2] == hw &&
                      s.y[2] == hh && s.x[3] == -hw && s.y[3] == hh;
  const bool opaque = std::all_of(std::begin(s.colour), std::end(s.colour), [](uint32_t c) { return c >> 24 == 0xFF; });
  return canvas && opaque && s.tw == 2 * hw && s.th == 2 * hh;
}

// What surrounds a full-screen picture: its edge texels in its own colours, or black.
enum class CanvasFill { kEdge, kBlack };

// The logo motion has loaded, its texture not seen yet.
inline constexpr uint32_t kLogoArmed = 1;

// The fill for a full-screen picture of `texture`. `logo` is the logo's texture: kLogoArmed when its motion loads,
// then the first picture's texture; another picture's texture (the title) ends it for good (0).
constexpr CanvasFill PictureFill(uint32_t& logo, uint32_t texture) {
  if (logo == kLogoArmed) {
    logo = texture;
  }
  if (logo != 0 && logo == texture) {
    return CanvasFill::kEdge;
  }
  logo = 0;
  return CanvasFill::kBlack;
}

// The title motion has loaded, its background's texture not seen yet.
inline constexpr uint32_t kTitleArmed = 1;

// Whether a full-screen picture of `texture` is the title's background. `title` is its texture: kTitleArmed when
// the title motion loads, then the first picture's texture; another picture's texture ends it for good (0).
constexpr bool IsTitlePicture(uint32_t& title, uint32_t texture) {
  if (title == kTitleArmed) {
    title = texture;
  }
  if (title != 0 && title == texture) {
    return true;
  }
  title = 0;
  return false;
}

// The title screen loads: its background is armed, and a logo that has drawn no picture yet is over, so the title's
// background is never taken for it. A logo that has drawn keeps its texture: its last frames (M2) still draw after
// the title loads, until the title's own first picture ends it.
constexpr void ArmTitle(uint32_t& logo, uint32_t& title) {
  if (logo == kLogoArmed) {
    logo = 0;
  }
  title = kTitleArmed;
}

// How a full-screen picture is drawn.
enum class PictureDraw { kCover, kEdgeBands, kBlackBands };

// The draw for a full-screen picture of `texture` from both words: the logo's texture first (edge bands), then the
// title's background (cover), black bands for any other picture.
constexpr PictureDraw ClassifyPicture(uint32_t& logo, uint32_t& title, uint32_t texture) {
  if (PictureFill(logo, texture) == CanvasFill::kEdge) {
    return PictureDraw::kEdgeBands;
  }
  return IsTitlePicture(title, texture) ? PictureDraw::kCover : PictureDraw::kBlackBands;
}

// A full-screen picture covering a w x h frame: its corners scaled about the canvas centre by the smallest factor,
// at least 1, that covers the frame's design canvas (W and H over the UI scale, centred on 0). Texels and colours
// stay, so the art keeps its proportions and is cropped centred. At 16:9 it is the game's sprite.
constexpr CanvasSprite CoverPicture(const CanvasSprite& s, uint32_t w, uint32_t h) {
  const double half_h = double(UiDesignHeight(w, h)) / 2;
  const double half_w = half_h * w / h;
  const double k = std::max({1.0, half_w / kCanvasHalfWidth, half_h / kCanvasHalfHeight});
  CanvasSprite out = s;
  for (int i = 0; i < 4; ++i) {
    out.x[i] = float(s.x[i] * k);
    out.y[i] = float(s.y[i] * k);
  }
  return out;
}

// The 8 bands around a full-screen picture out to `reach` units past it: left, right, top, bottom, then the corners
// top-left, top-right, bottom-right, bottom-left. kEdge bands sample the picture's edge column, row or corner texel
// (zero texel width across the band) in the colours of the nearest corners; kBlack bands are opaque black.
constexpr std::array<CanvasSprite, 8> CanvasBands(const CanvasSprite& s, CanvasFill fill, float reach) {
  const float x0 = s.x[0], y0 = s.y[0], x1 = s.x[2], y1 = s.y[2];
  const float left = s.tx + 0.5f, right = s.tx + s.tw - 0.5f, top = s.ty + 0.5f, bottom = s.ty + s.th - 0.5f;
  const uint32_t tl = s.colour[0], tr = s.colour[1], br = s.colour[2], bl = s.colour[3];
  const auto band = [&](float bx0, float by0, float bx1, float by1, float tx, float ty, float tw, float th,
                        std::array<uint32_t, 4> c) {
    CanvasSprite b{{bx0, bx1, bx1, bx0}, {by0, by0, by1, by1}, tx, ty, tw, th, {c[0], c[1], c[2], c[3]}};
    if (fill == CanvasFill::kBlack) {
      std::fill(std::begin(b.colour), std::end(b.colour), kCanvasBlack);
    }
    return b;
  };
  return {band(x0 - reach, y0, x0, y1, left, s.ty, 0, s.th, {tl, tl, bl, bl}),
          band(x1, y0, x1 + reach, y1, right, s.ty, 0, s.th, {tr, tr, br, br}),
          band(x0, y0 - reach, x1, y0, s.tx, top, s.tw, 0, {tl, tr, tr, tl}),
          band(x0, y1, x1, y1 + reach, s.tx, bottom, s.tw, 0, {bl, br, br, bl}),
          band(x0 - reach, y0 - reach, x0, y0, left, top, 0, 0, {tl, tl, tl, tl}),
          band(x1, y0 - reach, x1 + reach, y0, right, top, 0, 0, {tr, tr, tr, tr}),
          band(x1, y1, x1 + reach, y1 + reach, right, bottom, 0, 0, {br, br, br, br}),
          band(x0 - reach, y1, x0, y1 + reach, left, bottom, 0, 0, {bl, bl, bl, bl})};
}

}  // namespace futari::screen_layout
