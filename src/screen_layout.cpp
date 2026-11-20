// The game's frame at the output size (renut_resolution = output, on Renut only). The game picks its frame from a
// three-entry table (1280x720, 1920x1080, 1920x960) in sub_8216EBE8 and its screen-size floats from the same rule in
// sub_82181C60; both originals run unchanged, then these post-patches write the output's size into exactly the fields
// that carry it. Everything after them (the device constructor, the present parameters, Renut's CreateDevice) follows
// the presentation block. Under a preset, on the xenos plugin (also after the stale-package fallback) and on deko3d
// they write nothing. Under the same gate the menu wallpaper covers the frame at every aspect, every frame without
// it is cleared, the title's background covers the frame too, and the frame around any other full-screen picture
// is filled: the boot logos' own edge, black around the rest (the overrides at the end). On Renut a swap hook
// follows the window: a settled new output size resizes the device's buffers, then the same fields are rewritten.
// It also switches between output and a preset at once (to the output, or to the game's own table frame), and holds
// Renut's switch of scale until the frame is made for it. The gameplay zoom's default, maximum and value text follow
// the frame under output too; only the maximum's "never below the stored value" applies in every mode.

#include "generated/default/futari_init.h"

#include "screen_layout.h"
#include "screen_layout_math.h"
#include "smoothing.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <optional>
#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>

#if FUTARI_RENUT_RENDERER
#include <rex/ppc.h>
#include <rex/system/kernel_state.h>

#include "renut_engine/gpu/backend/presented_frame.h"
#include "renut_engine/gpu/banjo_renut_select.h"
#endif

// Dev aid: draw the game's frame at a size the monitor cannot show; the window stays as it is and the present fits
// the frame into it. Init-only, meant for the command line with the F9 captures:
//   --futari_dev_frame_size=3840x2160 --futari_renut_capture_path=out/f.ppm
REXCVAR_DEFINE_STRING(futari_dev_frame_size, "", "Futari",
                      "Dev: the game's frame size WxH under renut_resolution = output (empty: the output's size)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// Dev aid: draw gameplay at a chosen zoom value v (2 + v/80 Arcade pixels) without touching the save, for captures:
//   --futari_dev_zoom=110
REXCVAR_DEFINE_STRING(futari_dev_zoom, "", "Futari",
                      "Dev: the gameplay zoom value (at least -120) under renut_resolution = output (empty: the save's)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {

using futari::screen_layout::ArmTitle;
using futari::screen_layout::BlitTransform;
using futari::screen_layout::BlitWholePixelShift;
using futari::screen_layout::CanvasBands;
using futari::screen_layout::CanvasFill;
using futari::screen_layout::CanvasSprite;
using futari::screen_layout::ClampFrame;
using futari::screen_layout::ClassifyPicture;
using futari::screen_layout::CoverPicture;
using futari::screen_layout::FrameApplied;
using futari::screen_layout::FrameMode;
using futari::screen_layout::FrameReady;
using futari::screen_layout::FrameSize;
using futari::screen_layout::FrameTarget;
using futari::screen_layout::IsFullCanvasPicture;
using futari::screen_layout::IsWidescreen;
using futari::screen_layout::kCanvasBandReach;
using futari::screen_layout::kLogoArmed;
using futari::screen_layout::kNarrowUiCentring;
using futari::screen_layout::kZoomLabelScale;
using futari::screen_layout::ParseDevZoom;
using futari::screen_layout::ParseFrameSize;
using futari::screen_layout::PictureDraw;
using futari::screen_layout::PlanFrame;
using futari::screen_layout::PresetTargets;
using futari::screen_layout::QuadShift;
using futari::screen_layout::ResizeTargets;
using futari::screen_layout::ResizeValues;
using futari::screen_layout::UiDesignHeight;
using futari::screen_layout::WallpaperCover;
using futari::screen_layout::WallpaperQuad;
using futari::screen_layout::WallpaperSlide;
using futari::screen_layout::ZoomBound;
using futari::screen_layout::ZoomDefault;
using futari::screen_layout::ZoomLabelName;
using futari::screen_layout::ZoomMax;
using futari::screen_layout::ZoomMultiplier;
using futari::screen_layout::ZoomText;

// The presentation block sub_8216EBE8 fills: +56/+60 the reported mode (left alone), +64/+68 the frame size, +73 the
// widescreen byte, +80 the UI's design height (the device constructor's scale is +68 / +80).
constexpr uint32_t kPresentationBlock = 0x821ECEF0;
// sub_82181C60's screen-size floats (W, H), the game's widescreen flag (u32) and the guard that runs its body once.
constexpr uint32_t kScreenWidth = 0x821EC940;
constexpr uint32_t kScreenHeight = 0x821EC944;
constexpr uint32_t kWidescreenFlag = 0x821F8238;
constexpr uint32_t kScreenSizeOnce = 0x821F8590;
// The byte sub_82173200 passes as "clear the frame"; only the wallpaper function writes it: 0 while a layer exists
// (its opaque quad is already in the frame when the clear runs, so the clear would wipe it), 1 when it draws nothing.
constexpr uint32_t kClearFrame = 0x821EC65B;
// The wallpaper function sub_82184CC8 builds its quad on its stack at entry r1 - 304 and draws it with
// sub_8206BF48(6, decl, 4, vertices): 4 vertices of 16 bytes (x, y, u, v), v0 (x0, y0), v1 (x1, y0), v2 (x0, y1),
// v3 (x1, y1); u is u0 on v0 and v2 (0, or 0.25 on its 4:3 branch), 1 on v1 and v3.
constexpr uint32_t kWallpaperVerticesBelowEntry = 304;
constexpr uint32_t kWallpaperVertexCount = 4;
constexpr uint32_t kWallpaperVertexStride = 16;

// The wallpaper's vertex address while sub_82184CC8 runs on this thread under output; 0 otherwise.
thread_local uint32_t g_wallpaper_vertices = 0;

// Set when the wallpaper's quad is drawn, taken by the next frame clear.
std::atomic<bool> g_wallpaper_drawn{false};

// The game's textured sprite (sub_8216DAA0's second argument): +0 its texture, +4 a mode (0 plain), +8/+12 the
// texel width and height, +16/+20 the texel x and y, then 4 corners of 12 bytes from +24 (x, y in design units, an
// ARGB colour): top-left, top-right, bottom-right, bottom-left.
constexpr uint32_t kSpriteTexture = 0;
constexpr uint32_t kSpriteMode = 4;
// The game branches on the mode's low 3 bits (sub_8216DAA0: clrlwi r10,r11,29).
constexpr uint32_t kSpriteModeMask = 7;
constexpr uint32_t kSpriteModePlain = 0;
constexpr uint32_t kSpriteTexels = 8;
constexpr uint32_t kSpriteCorners = 24;
constexpr uint32_t kSpriteCornerStride = 12;
// The stack frame the extra sprites (bands, the covering title) are drawn from: the 72-byte sprite at +16, the
// draw's own frame below it.
constexpr uint32_t kBandFrame = 112;
constexpr uint32_t kBandSpriteOffset = 16;

// The logo's and the title background's textures for ClassifyPicture: kLogoArmed once the logo screen loads,
// kTitleArmed (ArmTitle) once the title screen loads, 0 = none. The title atlas's unload is not hooked: its word ends
// only at the next other full-screen picture.
std::atomic<uint32_t> g_logo_texture{0};
std::atomic<uint32_t> g_title_texture{0};

// The display block (the settings object + 1408, also at kDisplayBlockPointer): +8 the gameplay zoom as a float (2v),
// +60 the screen mode (0 normal, 1 its variant, 2 separate x/y sizes; the zoom row is shown in modes 0 and 1).
using futari::smoothing::kDisplayBlockPointer;
constexpr uint32_t kSettingsDisplayBlock = 1408;
constexpr uint32_t kDisplayZoom = 8;
constexpr uint32_t kDisplayScreenMode = 60;
constexpr uint32_t kScreenModeSeparateSizes = 2;
// The game's label scale (multiplier = float(4v + 640) * it), checked once against kZoomLabelScale.
constexpr uint32_t kGuestZoomLabelScale = 0x8204F96C;
// A Screen page (the r3 of its item load, sub_8219D980 or sub_8219DAC0): its items from +324 to +328, 80 bytes each.
// An item: +28 its id, +36 its type (1 = a number), +40 the value, +52 the maximum. The zoom (the active screen
// group's +8) is id 5 on the Layout page and id 2 on the Simple page (their accessors sub_8219D4E8, sub_8219D5E8).
constexpr uint32_t kPageItemsBegin = 324;
constexpr uint32_t kPageItemsEnd = 328;
constexpr uint32_t kItemStride = 80;
constexpr uint32_t kItemId = 28;
constexpr uint32_t kItemType = 36;
constexpr uint32_t kItemValue = 40;
constexpr uint32_t kItemMax = 52;
constexpr uint32_t kLayoutZoomItemId = 5;
constexpr uint32_t kSimpleZoomItemId = 2;
constexpr uint32_t kNumberItemType = 1;

// The zoom item the last Screen page load left, address in the high half and its id in the low; 0 = none seen.
std::atomic<uint64_t> g_zoom_item{0};
// The zoom item while sub_821B5D28 writes its row on this thread under output; 0 otherwise.
thread_local uint32_t g_zoom_text_item = 0;

bool IsZoomItem(uint8_t* base, uint32_t item, uint32_t zoom_id) {
  return REX_LOAD_U32(item + kItemId) == zoom_id && REX_LOAD_U32(item + kItemType) == kNumberItemType;
}

// Width in the high half, height in the low; 0 = not known.
std::atomic<uint64_t> g_configured_size{0};
std::atomic<uint64_t> g_window_size{0};

uint64_t Pack(uint32_t width, uint32_t height) { return uint64_t(width) << 32 | height; }
FrameSize Unpack(uint64_t size) { return {uint32_t(size >> 32), uint32_t(size)}; }

// The frame the game draws now: the boot size (the table's under a preset), then each live resize's. 0 = not booted.
std::atomic<uint64_t> g_current_frame{0};
// The mode that frame is made for: true output, false a preset. Read by the latch's gate on the render thread.
std::atomic<bool> g_frame_output{false};

bool OutputFrameActive() {
#if FUTARI_RENUT_RENDERER
  return banjo::renut_gpu::Active() && renut::gpu::ResolutionIsOutput();
#else
  return false;
#endif
}

// The output's size at boot: futari_dev_frame_size, else the window's client area (physical pixels; the monitor in
// fullscreen), else [Display] resolution, else the game's own 1280x720; clamped.
FrameSize BootFrameSize() {
  const char* source = "futari_dev_frame_size";
  uint64_t size = 0;
  if (const std::string dev = REXCVAR_GET(futari_dev_frame_size); !dev.empty()) {
    if (const std::optional<FrameSize> parsed = ParseFrameSize(dev)) {
      size = Pack(parsed->width, parsed->height);
    } else {
      REXLOG_WARN("futari_dev_frame_size = \"{}\" is not WxH; ignored", dev);
    }
  }
  if (!size) {
    source = "the window";
    size = g_window_size.load(std::memory_order_acquire);
  }
  if (!size) {
    source = "[Display] resolution";
    size = g_configured_size.load(std::memory_order_acquire);
  }
  if (!size) {
    source = "the default";
    size = Pack(1280, 720);
  }
  const uint32_t width = uint32_t(size >> 32);
  const uint32_t height = uint32_t(size);
  const FrameSize frame = ClampFrame(width, height);
  if (frame.width != width || frame.height != height) {
    REXLOG_WARN("Game frame: {}x{} from {} is outside 640..8192 x 480..8192; the game draws {}x{}", width, height,
                source, frame.width, frame.height);
  }
  REXLOG_INFO("Game frame {}x{} (output, from {}, {})", frame.width, frame.height, source,
              IsWidescreen(frame.width, frame.height) ? "widescreen" : "4:3 branches");
  g_current_frame.store(Pack(frame.width, frame.height), std::memory_order_release);
  return frame;
}

// Both post-patches write the same size, taken (and logged) once.
FrameSize BootFrame() {
  static const FrameSize frame = BootFrameSize();
  return frame;
}

// For the writers that follow a live resize.
FrameSize CurrentFrame() {
  const uint64_t size = g_current_frame.load(std::memory_order_acquire);
  return size ? Unpack(size) : BootFrame();
}

// After a Screen page loads its items' values: its zoom item (zoom_id, a number; the page's other items have other
// ids) gets the frame's maximum under output, never below its value: the game turns a value above the maximum into 0.
// In every other mode only that last part, so a value set under output survives a preset or xenos.
void BoundZoomItems(uint8_t* base, uint32_t page, uint32_t zoom_id) {
  const bool output = OutputFrameActive();
  const uint32_t end = REX_LOAD_U32(page + kPageItemsEnd);
  for (uint32_t item = REX_LOAD_U32(page + kPageItemsBegin); item + kItemStride <= end; item += kItemStride) {
    if (!IsZoomItem(base, item, zoom_id)) {
      continue;
    }
    const int32_t v = int32_t(REX_LOAD_U32(item + kItemValue));
    int32_t max = int32_t(REX_LOAD_U32(item + kItemMax));
    if (output) {
      const FrameSize frame = CurrentFrame();
      max = ZoomMax(frame.width, frame.height);
    }
    max = ZoomBound(max, v);
    REX_STORE_U32(item + kItemMax, uint32_t(max));
    g_zoom_item.store(uint64_t(item) << 32 | zoom_id, std::memory_order_relaxed);
    static uint64_t logged = 0;
    if (const uint64_t now = uint64_t(uint32_t(v)) << 32 | uint32_t(max); now != logged) {
      logged = now;
      REXLOG_INFO("Zoom item: value {}, max {}", v, max);
    }
  }
}

#if FUTARI_RENUT_RENDERER
// The renderer object (inferred: the global the UI matrix sub_8216DD68 takes it from): +72 the mode its centred rect
// was built with (sub_8216B6A8), +128/+132 its constructor's float copy of block +64/+68, +152 the game device.
constexpr uint32_t kRendererObject = 0x821F85B0;
constexpr uint32_t kRendererMode = 72;
constexpr uint32_t kRendererWidth = 128;
constexpr uint32_t kRendererHeight = 132;
constexpr uint32_t kRendererDevice = 152;
// The game device's UI scale (its constructor's H / block +80), read every frame by the UI matrix.
constexpr uint32_t kDeviceUiScale = 556;
// The D3D device's PA_SU_VTX_CNTL word: bit 0 (pix_center, D3DRS_HALFPIXELOFFSET) set puts pixel centres at half
// pixels, so Renut's viewport takes no half-pixel origin.
constexpr uint32_t kD3DVertexControl = 0x29C0;
constexpr uint32_t kPixelCentreBit = 1;
// The blit's quad: 4 vertices of 16 bytes (x, y, u, v), and its VS rows (clip x, clip y), 16 bytes each.
constexpr uint32_t kBlitVertexCount = 4;
constexpr uint32_t kBlitVertexStride = 16;
constexpr uint32_t kBlitMatrixRow = 16;

// The D3D device the game swaps (its guest address); 0 before the first swap.
std::atomic<uint32_t> g_d3d_device{0};
// The gameplay quad's shift and frame size last logged (x, y, w, h), per guest thread that draws the blit.
thread_local std::optional<std::array<float, 4>> g_logged_shift;

uint8_t* GuestBase() { return rex::system::kernel_state()->memory()->virtual_membase(); }

// The size futari_dev_frame_size names: under output the frame stays at it.
std::optional<FrameSize> DevFrame() {
  static const std::optional<FrameSize> frame = ParseFrameSize(REXCVAR_GET(futari_dev_frame_size));
  return frame;
}

// The renderer object, when its frame floats are 'frame' (so it is the object the constructor filled): else 0.
uint32_t RendererAt(uint8_t* base, FrameSize frame) {
  const uint32_t renderer = REX_LOAD_U32(kRendererObject);
  if (renderer == 0 || REX_LOAD_U32(renderer + kRendererDevice) == 0 ||
      REX_LOAD_U32(renderer + kRendererWidth) != std::bit_cast<uint32_t>(float(frame.width)) ||
      REX_LOAD_U32(renderer + kRendererHeight) != std::bit_cast<uint32_t>(float(frame.height))) {
    return 0;
  }
  return renderer;
}

// Once, at the first swap: what the renderer object holds against the block, for the run's log.
void LogRendererOnce(uint8_t* base) {
  static bool logged = false;
  if (logged) {
    return;
  }
  logged = true;
  const uint32_t renderer = REX_LOAD_U32(kRendererObject);
  if (renderer == 0) {
    REXLOG_INFO("Renderer object: none at {:08X}", kRendererObject);
    return;
  }
  REXLOG_INFO("Renderer object {:08X}: frame floats {}x{}, device {:08X}; block frame {}x{}", renderer,
              std::bit_cast<float>(REX_LOAD_U32(renderer + kRendererWidth)),
              std::bit_cast<float>(REX_LOAD_U32(renderer + kRendererHeight)),
              REX_LOAD_U32(renderer + kRendererDevice), REX_LOAD_U32(kPresentationBlock + 64),
              REX_LOAD_U32(kPresentationBlock + 68));
}

// The frame size this swap should resize to, and its mode, or nothing (PlanFrame's rules): a new renut_resolution
// at once, else under output the output's size once it has settled.
std::optional<FrameTarget> NextFrame(FrameMode& mode, FrameSize reported) {
  const bool output = renut::gpu::ResolutionRequestsOutput();
  const renut::gpu::OutputPixels size = renut::gpu::OutputSize();
  const FrameSize clamped = ClampFrame(size.width, size.height);
  static bool warned = false;
  if (output && !warned && size.width && size.height &&
      (clamped.width != size.width || clamped.height != size.height)) {
    warned = true;
    REXLOG_WARN("Game frame: the output {}x{} is outside 640..8192 x 480..8192; the game draws {}x{}", size.width,
                size.height, clamped.width, clamped.height);
  }
  return PlanFrame(mode, output, {size.width, size.height}, DevFrame(), reported);
}

// After the device's buffers are v's size: the game's own copies of the frame, in the order a boot fills them. The
// renderer object (and the device it holds) only when it still holds 'from', so a wrong guess is never written.
void WriteFrame(FrameSize from, const ResizeValues& v) {
  uint8_t* const base = GuestBase();
  const uint32_t renderer = RendererAt(base, from);
  REX_STORE_U32(kPresentationBlock + 64, v.width);
  REX_STORE_U32(kPresentationBlock + 68, v.height);
  REX_STORE_U8(kPresentationBlock + 73, v.widescreen);
  REX_STORE_U32(kPresentationBlock + 80, std::bit_cast<uint32_t>(v.ui_design_height));
  REX_STORE_U32(kScreenWidth, std::bit_cast<uint32_t>(v.screen_width));
  REX_STORE_U32(kScreenHeight, std::bit_cast<uint32_t>(v.screen_height));
  REX_STORE_U32(kWidescreenFlag, v.widescreen_flag);
  if (renderer != 0) {
    REX_STORE_U32(REX_LOAD_U32(renderer + kRendererDevice) + kDeviceUiScale, std::bit_cast<uint32_t>(v.ui_scale));
    REX_STORE_U32(renderer + kRendererWidth, std::bit_cast<uint32_t>(v.renderer_width));
    REX_STORE_U32(renderer + kRendererHeight, std::bit_cast<uint32_t>(v.renderer_height));
    // Rebuilds its centred rect (+76..+104) from the block.
    rex::ppc::GuestToHostFunction<void>(sub_8216B6A8, renderer, REX_LOAD_U32(renderer + kRendererMode));
  } else {
    static bool warned = false;
    if (!warned) {
      warned = true;
      REXLOG_WARN("Game frame: the renderer object at {:08X} doesn't hold the {}x{} frame; its frame and the UI "
                  "scale are left as they are",
                  kRendererObject, from.width, from.height);
    }
  }
  g_current_frame.store(Pack(v.width, v.height), std::memory_order_release);
}

const char* ModeName(bool output) { return output ? "output" : "preset"; }

// Every Swap, on the guest thread before the frame is queued: a switch between output and a preset resizes the
// game's frame at once; under output (without a fixed dev size) a settled window size resizes it live.
void GuestSwap(uint32_t device) {
  g_d3d_device.store(device, std::memory_order_relaxed);
  const uint64_t current = g_current_frame.load(std::memory_order_acquire);
  if (current == 0) {
    return;
  }
  uint8_t* const base = GuestBase();
  LogRendererOnce(base);
  static FrameMode mode = [current] {
    const bool output = g_frame_output.load(std::memory_order_acquire);
    return FrameMode{{Unpack(current), Unpack(current), 0}, output, output};
  }();
  // The mode the game reported at boot (+56/+60, left alone by the post-patch): the window doesn't change it.
  const FrameSize reported{REX_LOAD_U32(kPresentationBlock + 56), REX_LOAD_U32(kPresentationBlock + 60)};
  const std::optional<FrameTarget> target = NextFrame(mode, reported);
  if (!target) {
    return;
  }
  const FrameSize from = mode.settle.frame;
  const bool switched = target->output != mode.output;
  // On failure Renut has warned and nothing changed: the old frame (and mode: the gate stays closed) stays until the
  // size or the mode asked for changes again.
  if (!renut::gpu::ResizeAutoBuffers(device, target->frame.width, target->frame.height)) {
    if (switched) {
      REXLOG_WARN("Game frame: the switch to {} ({}x{}) failed; {} {}x{} stays", ModeName(target->output),
                  target->frame.width, target->frame.height, ModeName(mode.output), from.width, from.height);
    }
    return;
  }
  WriteFrame(from, target->output ? ResizeTargets(target->frame.width, target->frame.height)
                                  : PresetTargets(reported.width, reported.height));
  FrameApplied(mode, *target);
  g_frame_output.store(target->output, std::memory_order_release);
  if (switched) {
    REXLOG_INFO("Game frame {}x{} ({}, switched from {} {}x{})", target->frame.width, target->frame.height,
                ModeName(target->output), ModeName(!target->output), from.width, from.height);
  } else {
    REXLOG_INFO("Game frame {}x{} (output, live resize from {}x{})", target->frame.width, target->frame.height,
                from.width, from.height);
  }
}

// Renut's latch gate (render thread): a new renut_resolution is taken once the game's frame is made for it.
bool FrameReadyFor(bool output, uint32_t) {
  return FrameReady(g_frame_output.load(std::memory_order_acquire), output);
}

// Renut's viewport origin for the device's draws, as its draw.cpp sets it: half a pixel with
// banjo_renut_half_pixel_offset (Renut's own latch) unless the device's pix_center bit is set (the device starts with
// it clear; before the first swap the device isn't known, so clear).
float RenutPixelCentre(uint8_t* base) {
  const uint32_t device = g_d3d_device.load(std::memory_order_relaxed);
  const bool pixel_centre_bit = device != 0 && (REX_LOAD_U32(device + kD3DVertexControl) & kPixelCentreBit) != 0;
  return renut::gpu::HalfPixelOffsetOn() && !pixel_centre_bit ? 0.5f : 0.0f;
}

// The gameplay blit's own draw under output: its quad moves by under a pixel (x, y of all 4 vertices, the same
// amount; u, v untouched) so its edges land on whole frame pixels at odd frame sizes too. Written in place and left:
// the game's later passes over the same quad stay aligned with it (sub_82183658 also reads the reused quad; that
// consumer is untraced). A matrix that isn't a 90-degree turn at scale 1
// (a rotation ease) leaves the quad alone.
void SnapBlitQuad(uint8_t* base, uint32_t vertices, uint32_t matrix) {
  BlitTransform transform;
  for (uint32_t i = 0; i < 4; ++i) {
    transform.r0[i] = std::bit_cast<float>(REX_LOAD_U32(matrix + 4 * i));
    transform.r1[i] = std::bit_cast<float>(REX_LOAD_U32(matrix + kBlitMatrixRow + 4 * i));
  }
  const float width = std::bit_cast<float>(REX_LOAD_U32(kScreenWidth));
  const float height = std::bit_cast<float>(REX_LOAD_U32(kScreenHeight));
  const float x0 = std::bit_cast<float>(REX_LOAD_U32(vertices));
  const float y0 = std::bit_cast<float>(REX_LOAD_U32(vertices + 4));
  const std::optional<QuadShift> shift = BlitWholePixelShift(transform, width, height, RenutPixelCentre(base), x0, y0);
  if (!shift) {
    return;
  }
  if (shift->x != 0 || shift->y != 0) {
    for (uint32_t i = 0; i < kBlitVertexCount; ++i) {
      const uint32_t vertex = vertices + i * kBlitVertexStride;
      REX_STORE_U32(vertex + 0, std::bit_cast<uint32_t>(std::bit_cast<float>(REX_LOAD_U32(vertex + 0)) + shift->x));
      REX_STORE_U32(vertex + 4, std::bit_cast<uint32_t>(std::bit_cast<float>(REX_LOAD_U32(vertex + 4)) + shift->y));
    }
  }
  if (const std::array<float, 4> now{shift->x, shift->y, width, height}; g_logged_shift != now) {
    g_logged_shift = now;
    if (shift->x == 0 && shift->y == 0) {
      REXLOG_INFO("Gameplay quad already on whole pixels at {}x{}", width, height);
    } else {
      REXLOG_INFO("Gameplay quad shifted ({}, {}) to whole pixels at {}x{}", shift->x, shift->y, width, height);
    }
  }
}
#endif

}  // namespace

namespace futari::screen_layout {

void SetConfiguredSize(uint32_t width, uint32_t height) {
  g_configured_size.store(Pack(width, height), std::memory_order_release);
}

void SetWindowPixelSize(uint32_t width, uint32_t height) {
  if (width && height) {
    g_window_size.store(Pack(width, height), std::memory_order_release);
  }
}

}  // namespace futari::screen_layout

// The mode table: +64/+68 = the frame, +73 = the game's widescreen rule for it.
REX_EXTERN(__imp__sub_8216EBE8);
extern "C" REX_FUNC(sub_8216EBE8) {
  __imp__sub_8216EBE8(ctx, base);
#if FUTARI_RENUT_RENDERER
  if (banjo::renut_gpu::Active()) {
    static const bool registered = [] {
      g_frame_output.store(OutputFrameActive(), std::memory_order_release);
      renut::gpu::SetResolutionLatchGate(&FrameReadyFor);
      renut::gpu::SetGuestSwapHook(&GuestSwap);
      return true;
    }();
    (void)registered;
    // Under a preset the game's own table frame is the current frame.
    if (!OutputFrameActive()) {
      g_current_frame.store(Pack(REX_LOAD_U32(kPresentationBlock + 64), REX_LOAD_U32(kPresentationBlock + 68)),
                            std::memory_order_release);
    }
  }
#endif
  if (!OutputFrameActive()) {
    return;
  }
  const FrameSize frame = BootFrame();
  REX_STORE_U32(kPresentationBlock + 64, frame.width);
  REX_STORE_U32(kPresentationBlock + 68, frame.height);
  REX_STORE_U8(kPresentationBlock + 73, IsWidescreen(frame.width, frame.height) ? 1 : 0);
  // Below 4:3 the UI's design height grows so the device's scale (H / +80) fits the 4:3 content to the width.
  if (!IsWidescreen(frame.width, frame.height)) {
    REX_STORE_U32(kPresentationBlock + 80, std::bit_cast<uint32_t>(UiDesignHeight(frame.width, frame.height)));
  }
}

// The 4:3 layout's x offset (+80 * 16/9 - +80 * reported w/h): below 4:3 under output, the console's 4:3 value, so
// it neither follows the grown +80 nor the reported mode.
REX_EXTERN(__imp__sub_821CB2E0);
extern "C" REX_FUNC(sub_821CB2E0) {
  __imp__sub_821CB2E0(ctx, base);
  if (OutputFrameActive() && REX_LOAD_U8(kPresentationBlock + 73) == 0) {
    ctx.f1.f64 = double(kNarrowUiCentring);
  }
}

// The screen-size floats and the game's widescreen flag, on the one call whose body ran.
REX_EXTERN(__imp__sub_82181C60);
extern "C" REX_FUNC(sub_82181C60) {
  const bool first = REX_LOAD_U32(kScreenSizeOnce) == 0;
  __imp__sub_82181C60(ctx, base);
  if (!first || !OutputFrameActive()) {
    return;
  }
  const FrameSize frame = BootFrame();
  REX_STORE_U32(kScreenWidth, std::bit_cast<uint32_t>(float(frame.width)));
  REX_STORE_U32(kScreenHeight, std::bit_cast<uint32_t>(float(frame.height)));
  REX_STORE_U32(kWidescreenFlag, IsWidescreen(frame.width, frame.height) ? 1u : 0u);
}

// The menu wallpaper: its quad only fits a 16:9 frame, so mark its draw for sub_8206BF48 below.
REX_EXTERN(__imp__sub_82184CC8);
extern "C" REX_FUNC(sub_82184CC8) {
  if (!OutputFrameActive()) {
    __imp__sub_82184CC8(ctx, base);
    return;
  }
  g_wallpaper_vertices = ctx.r1.u32 - kWallpaperVerticesBelowEntry;
  __imp__sub_82184CC8(ctx, base);
  g_wallpaper_vertices = 0;
}

// The generic draw-from-memory: on the wallpaper's own quad only, cover the frame with the 16:9 art at the game's
// slide-in offset, then draw. On the gameplay blit's own quad under output, move it onto whole frame pixels. Every
// other draw passes through (the gameplay blit's quad is logged for dev captures, as drawn).
REX_EXTERN(__imp__sub_8206BF48);
extern "C" REX_FUNC(sub_8206BF48) {
  const uint32_t vertices = ctx.r6.u32;
#if FUTARI_RENUT_RENDERER
  if (const uint32_t matrix = futari::smoothing::BlitDrawMatrix(vertices, ctx.r5.u32);
      matrix != 0 && OutputFrameActive()) {
    SnapBlitQuad(base, vertices, matrix);
  }
#endif
  futari::smoothing::NoteBlitDraw(base, vertices, ctx.r5.u32);
  if (g_wallpaper_vertices != 0 && vertices == g_wallpaper_vertices && ctx.r5.u32 == kWallpaperVertexCount) {
    const bool wide = REX_LOAD_U32(kWidescreenFlag) != 0;
    const float x0 = std::bit_cast<float>(REX_LOAD_U32(vertices));
    const WallpaperQuad quad = WallpaperCover(std::bit_cast<float>(REX_LOAD_U32(kScreenWidth)),
                                              std::bit_cast<float>(REX_LOAD_U32(kScreenHeight)),
                                              WallpaperSlide(x0, wide));
    const float xs[kWallpaperVertexCount] = {quad.x0, quad.x1, quad.x0, quad.x1};
    const float ys[kWallpaperVertexCount] = {quad.y0, quad.y0, quad.y1, quad.y1};
    for (uint32_t i = 0; i < kWallpaperVertexCount; ++i) {
      const uint32_t vertex = vertices + i * kWallpaperVertexStride;
      REX_STORE_U32(vertex + 0, std::bit_cast<uint32_t>(xs[i]));
      REX_STORE_U32(vertex + 4, std::bit_cast<uint32_t>(ys[i]));
      if (i % 2 == 0) {
        REX_STORE_U32(vertex + 8, std::bit_cast<uint32_t>(0.0f));  // v0, v2: u0 = 0, the whole art, not the 4:3 crop
      }
    }
    g_wallpaper_drawn.store(true, std::memory_order_relaxed);
  }
  __imp__sub_8206BF48(ctx, base);
}

// The frame clear: the game skips it whenever a wallpaper layer exists, also when nothing drew it (layers released,
// or the function not called), which leaves old frames in the borders a non-16:9 title or menu doesn't draw. Under
// output the frame is cleared unless the wallpaper's quad was drawn since the last clear.
REX_EXTERN(__imp__sub_82173200);
extern "C" REX_FUNC(sub_82173200) {
  if (OutputFrameActive() && !g_wallpaper_drawn.exchange(false, std::memory_order_relaxed)) {
    REX_STORE_U8(kClearFrame, 1);
  }
  __imp__sub_82173200(ctx, base);
}

// The logo screen's load (it loads motion/logo.lzx): the next full-screen picture is the logo's.
REX_EXTERN(__imp__sub_821D1B50);
extern "C" REX_FUNC(sub_821D1B50) {
  __imp__sub_821D1B50(ctx, base);
  if (OutputFrameActive()) {
    g_logo_texture.store(kLogoArmed, std::memory_order_relaxed);
  }
}

// The title screen's load (it loads motion/title.lzx): the next full-screen picture is the title's background.
REX_EXTERN(__imp__sub_821CC7A8);
extern "C" REX_FUNC(sub_821CC7A8) {
  __imp__sub_821CC7A8(ctx, base);
  if (OutputFrameActive()) {
    uint32_t logo = g_logo_texture.load(std::memory_order_relaxed);
    uint32_t title = 0;
    ArmTitle(logo, title);
    g_logo_texture.store(logo, std::memory_order_relaxed);
    g_title_texture.store(title, std::memory_order_relaxed);
  }
}

// The game's textured sprite draw. A full-screen picture (the boot logos, the title's background) gets more:
// the title's background is drawn covering the frame instead (a scaled copy, same texels, cropped centred like the
// menu wallpaper; its logo and texts are other sprites and stay put), which also hides the menu wallpaper the game
// still draws behind the title after the menus. Around any other picture the frame outside its 16:9 canvas gets 8
// more sprites: the logo's edge texels in its own colours, so its white and its fades reach the frame's edges, and
// black around the rest.
REX_EXTERN(__imp__sub_8216DAA0);
extern "C" REX_FUNC(sub_8216DAA0) {
  const uint32_t device = ctx.r3.u32;
  const uint32_t sprite = ctx.r4.u32;
  const uint32_t mode = REX_LOAD_U32(sprite + kSpriteMode);
  if (!OutputFrameActive() || (mode & kSpriteModeMask) != kSpriteModePlain) {
    __imp__sub_8216DAA0(ctx, base);
    return;
  }
  CanvasSprite picture;
  for (uint32_t i = 0; i < 4; ++i) {
    const uint32_t corner = sprite + kSpriteCorners + i * kSpriteCornerStride;
    picture.x[i] = std::bit_cast<float>(REX_LOAD_U32(corner + 0));
    picture.y[i] = std::bit_cast<float>(REX_LOAD_U32(corner + 4));
    picture.colour[i] = REX_LOAD_U32(corner + 8);
  }
  picture.tw = std::bit_cast<float>(REX_LOAD_U32(sprite + kSpriteTexels + 0));
  picture.th = std::bit_cast<float>(REX_LOAD_U32(sprite + kSpriteTexels + 4));
  picture.tx = std::bit_cast<float>(REX_LOAD_U32(sprite + kSpriteTexels + 8));
  picture.ty = std::bit_cast<float>(REX_LOAD_U32(sprite + kSpriteTexels + 12));
  if (!IsFullCanvasPicture(picture)) {
    __imp__sub_8216DAA0(ctx, base);
    return;
  }
  const uint32_t texture = REX_LOAD_U32(sprite + kSpriteTexture);
  uint32_t logo = g_logo_texture.load(std::memory_order_relaxed);
  uint32_t title = g_title_texture.load(std::memory_order_relaxed);
  const PictureDraw draw = ClassifyPicture(logo, title, texture);
  g_logo_texture.store(logo, std::memory_order_relaxed);
  g_title_texture.store(title, std::memory_order_relaxed);

  // Each extra sprite is a copy of the picture's sprite on the guest stack, below the caller's frame.
  const uint32_t stack = ctx.r1.u32;
  const uint32_t copy = stack - kBandFrame + kBandSpriteOffset;
  const auto draw_copy = [&](const CanvasSprite& s) {
    REX_STORE_U32(copy + kSpriteTexture, texture);
    REX_STORE_U32(copy + kSpriteMode, mode);
    REX_STORE_U32(copy + kSpriteTexels + 0, std::bit_cast<uint32_t>(s.tw));
    REX_STORE_U32(copy + kSpriteTexels + 4, std::bit_cast<uint32_t>(s.th));
    REX_STORE_U32(copy + kSpriteTexels + 8, std::bit_cast<uint32_t>(s.tx));
    REX_STORE_U32(copy + kSpriteTexels + 12, std::bit_cast<uint32_t>(s.ty));
    for (uint32_t i = 0; i < 4; ++i) {
      const uint32_t corner = copy + kSpriteCorners + i * kSpriteCornerStride;
      REX_STORE_U32(corner + 0, std::bit_cast<uint32_t>(s.x[i]));
      REX_STORE_U32(corner + 4, std::bit_cast<uint32_t>(s.y[i]));
      REX_STORE_U32(corner + 8, s.colour[i]);
    }
    ctx.r1.u32 = stack - kBandFrame;
    ctx.r3.u32 = device;
    ctx.r4.u32 = copy;
    __imp__sub_8216DAA0(ctx, base);
  };
  if (draw == PictureDraw::kCover) {
    const FrameSize frame = CurrentFrame();
    draw_copy(CoverPicture(picture, frame.width, frame.height));
  } else {
    __imp__sub_8216DAA0(ctx, base);
    const CanvasFill fill = draw == PictureDraw::kEdgeBands ? CanvasFill::kEdge : CanvasFill::kBlack;
    for (const CanvasSprite& band : CanvasBands(picture, fill, kCanvasBandReach)) {
      draw_copy(band);
    }
  }
  ctx.r1.u32 = stack;
}

// The zoom a new settings object's group 0 and the Screen page's Reset take: the frame's fit.
REX_EXTERN(__imp__sub_82185758);
extern "C" REX_FUNC(sub_82185758) {
  __imp__sub_82185758(ctx, base);
  if (!OutputFrameActive()) {
    return;
  }
  const FrameSize frame = CurrentFrame();
  const int32_t v = ZoomDefault(frame.width, frame.height);
  ctx.r3.s64 = v;
  static std::optional<int32_t> logged;
  if (logged != v) {
    logged = v;
    REXLOG_INFO("Zoom default {} ({:.2f}x) for {}x{}", v, ZoomMultiplier(v), frame.width, frame.height);
  }
}

// The Layout page loads its items' values.
REX_EXTERN(__imp__sub_8219D980);
extern "C" REX_FUNC(sub_8219D980) {
  const uint32_t page = ctx.r3.u32;
  __imp__sub_8219D980(ctx, base);
  BoundZoomItems(base, page, kLayoutZoomItemId);
}

// The Simple page (a fresh save's Screen page) loads its items' values.
REX_EXTERN(__imp__sub_8219DAC0);
extern "C" REX_FUNC(sub_8219DAC0) {
  const uint32_t page = ctx.r3.u32;
  __imp__sub_8219DAC0(ctx, base);
  BoundZoomItems(base, page, kSimpleZoomItemId);
}

// A menu row's text: on the zoom item under output, mark it for sub_8207A990 below, which prints its value.
// Screen mode 2 has no single zoom and keeps the game's text.
REX_EXTERN(__imp__sub_821B5D28);
extern "C" REX_FUNC(sub_821B5D28) {
  const uint32_t item = ctx.r4.u32;
  const uint64_t zoom = g_zoom_item.load(std::memory_order_relaxed);
  if (!OutputFrameActive() || item == 0 || item != uint32_t(zoom >> 32) || !IsZoomItem(base, item, uint32_t(zoom)) ||
      REX_LOAD_U32(REX_LOAD_U32(kDisplayBlockPointer) + kDisplayScreenMode) == kScreenModeSeparateSizes) {
    __imp__sub_821B5D28(ctx, base);
    return;
  }
  g_zoom_text_item = item;
  __imp__sub_821B5D28(ctx, base);
  g_zoom_text_item = 0;
}

// The game's _snprintf_s(buffer, size, format, value): for the marked zoom row, the value as its multiplier
// ("3.00x", " DbD" at integers in screen mode 0, as the game's label), else the game's own text.
REX_EXTERN(__imp__sub_8207A990);
extern "C" REX_FUNC(sub_8207A990) {
  const uint32_t buffer = ctx.r3.u32;
  const uint32_t size = ctx.r4.u32;
  const int32_t v = ctx.r6.s32;
  __imp__sub_8207A990(ctx, base);
  if (g_zoom_text_item == 0 || size == 0) {
    return;
  }
  static const bool checked = [base] {
    const float guest = std::bit_cast<float>(REX_LOAD_U32(kGuestZoomLabelScale));
    if (guest != kZoomLabelScale) {
      REXLOG_WARN("Zoom: the game's label scale at {:08X} is {}, not {}; the value text may round differently",
                  kGuestZoomLabelScale, guest, kZoomLabelScale);
    }
    return true;
  }();
  (void)checked;
  const uint32_t block = REX_LOAD_U32(kDisplayBlockPointer);
  const std::string text = ZoomText(v, REX_LOAD_U32(block + kDisplayScreenMode) == 0);
  const uint32_t length = std::min<uint32_t>(uint32_t(text.size()), size - 1);
  for (uint32_t i = 0; i < length; ++i) {
    REX_STORE_U8(buffer + i, uint8_t(text[i]));
  }
  REX_STORE_U8(buffer + length, 0);
  ctx.r3.s64 = length;
}

// The game's sprintf(buffer, format, ...) into a 128-byte buffer; its only caller is the Layout page's per-frame
// update sub_821A0760, which prints the zoom item's label with kZoomLabelFormat ("Zoom(x3.00 DbD)"). Under output the
// label ends before its "(" (the value text shows the multiplier); presets, xenos and screen mode 2 keep the game's.
constexpr uint32_t kZoomLabelFormat = 0x8204965C;
constexpr uint32_t kZoomLabelBufferSize = 128;
REX_EXTERN(__imp__sub_82069198);
extern "C" REX_FUNC(sub_82069198) {
  const uint32_t buffer = ctx.r3.u32;
  const uint32_t format = ctx.r4.u32;
  __imp__sub_82069198(ctx, base);
  if (format != kZoomLabelFormat || !OutputFrameActive() ||
      REX_LOAD_U32(REX_LOAD_U32(kDisplayBlockPointer) + kDisplayScreenMode) == kScreenModeSeparateSizes) {
    return;
  }
  std::string label;
  for (uint32_t i = 0; i < kZoomLabelBufferSize; ++i) {
    const char c = char(REX_LOAD_U8(buffer + i));
    if (c == '\0') {
      break;
    }
    label += c;
  }
  const uint32_t length = uint32_t(ZoomLabelName(label).size());
  if (length == label.size()) {
    return;
  }
  REX_STORE_U8(buffer + length, 0);
  ctx.r3.s64 = length;
}

// The display values from the settings: under output with futari_dev_zoom set, the gameplay size is its value's
// instead of the save's (the save is not written). Screen mode 2 has no single zoom and keeps the game's. With
// futari_dev_smoothing set, the Smoothing is its value, under every renderer.
REX_EXTERN(__imp__sub_82187EE0);
extern "C" REX_FUNC(sub_82187EE0) {
  const uint32_t settings = ctx.r3.u32;
  __imp__sub_82187EE0(ctx, base);
  futari::smoothing::ApplyDevSmoothing(base, settings + kSettingsDisplayBlock);
  static const std::optional<int32_t> zoom = [] {
    const std::string text = REXCVAR_GET(futari_dev_zoom);
    if (text.empty()) {
      return std::optional<int32_t>{};
    }
    const std::optional<int32_t> parsed = ParseDevZoom(text);
    if (!parsed) {
      REXLOG_WARN("futari_dev_zoom = \"{}\" is not a whole number of at least -120; ignored", text);
    }
    return parsed;
  }();
  if (!zoom || !OutputFrameActive()) {
    return;
  }
  const uint32_t block = settings + kSettingsDisplayBlock;
  if (REX_LOAD_U32(block + kDisplayScreenMode) == kScreenModeSeparateSizes) {
    return;
  }
  REX_STORE_U32(block + kDisplayZoom, std::bit_cast<uint32_t>(float(2.0 * *zoom)));
  static bool logged = false;
  if (!logged) {
    logged = true;
    REXLOG_INFO("Zoom {} ({:.2f}x) from futari_dev_zoom; the save keeps its own", *zoom, ZoomMultiplier(*zoom));
  }
}
