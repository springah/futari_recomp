// The game's frame at the output size (renut_resolution = output, on Renut only). The game picks its frame from a
// three-entry table (1280x720, 1920x1080, 1920x960) in sub_8216EBE8 and its screen-size floats from the same rule in
// sub_82181C60; both originals run unchanged, then these post-patches write the output's size into exactly the fields
// that carry it. Everything after them (the device constructor, the present parameters, Renut's CreateDevice) follows
// the presentation block. Under a preset, on the xenos plugin (also after the stale-package fallback) and on deko3d
// they write nothing.

#include "generated/default/futari_init.h"

#include "screen_layout.h"
#include "screen_layout_math.h"

#include <atomic>
#include <bit>
#include <optional>
#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>

#if FUTARI_RENUT_RENDERER
#include "renut_engine/gpu/banjo_renut_select.h"
#endif

// Dev aid: draw the game's frame at a size the monitor cannot show; the window stays as it is and the present fits
// the frame into it. Init-only, meant for the command line with the F9 captures:
//   --futari_dev_frame_size=3840x2160 --futari_renut_capture_path=out/f.ppm
REXCVAR_DEFINE_STRING(futari_dev_frame_size, "", "Futari",
                      "Dev: the game's frame size WxH under renut_resolution = output (empty: the output's size)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {

using futari::screen_layout::ClampFrame;
using futari::screen_layout::FrameSize;
using futari::screen_layout::IsWidescreen;
using futari::screen_layout::ParseFrameSize;

// The presentation block sub_8216EBE8 fills: +56/+60 the reported mode (left alone), +64/+68 the frame size, +73 the
// widescreen byte.
constexpr uint32_t kPresentationBlock = 0x821ECEF0;
// sub_82181C60's screen-size floats (W, H), the game's widescreen flag (u32) and the guard that runs its body once.
constexpr uint32_t kScreenWidth = 0x821EC940;
constexpr uint32_t kScreenHeight = 0x821EC944;
constexpr uint32_t kWidescreenFlag = 0x821F8238;
constexpr uint32_t kScreenSizeOnce = 0x821F8590;

// Width in the high half, height in the low; 0 = not known.
std::atomic<uint64_t> g_configured_size{0};
std::atomic<uint64_t> g_window_size{0};

uint64_t Pack(uint32_t width, uint32_t height) { return uint64_t(width) << 32 | height; }

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
  return frame;
}

// Both post-patches write the same size, taken (and logged) once.
FrameSize BootFrame() {
  static const FrameSize frame = BootFrameSize();
  return frame;
}

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
  if (!OutputFrameActive()) {
    return;
  }
  const FrameSize frame = BootFrame();
  REX_STORE_U32(kPresentationBlock + 64, frame.width);
  REX_STORE_U32(kPresentationBlock + 68, frame.height);
  REX_STORE_U8(kPresentationBlock + 73, IsWidescreen(frame.width, frame.height) ? 1 : 0);
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
