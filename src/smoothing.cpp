// The game's Smoothing with a third value, Sharp bilinear (4). The gameplay blit sub_82183C68 reads the mode once
// and skips its filter code for any value above 3, so for Sharp it runs as On (the linear filter and the On shader
// pair, the same draw on every renderer); on Renut the renderer is told the mode first and redraws that blit sharp
// (and On's with float bilinear, finer than the sampler's weights), on the default renderer it stays On. Dev aids for the capture runs: futari_dev_smoothing forces a mode without touching
// the save, and the blit's quad is logged once per change. The Picture page's Smoothing item reads Smooth / Sharp /
// Off: it holds a menu index, translated from the word on load and back to it on change.

#include "generated/default/futari_init.h"

#include "smoothing.h"
#include "smoothing_math.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <optional>
#include <string>
#include <string_view>

#include <rex/cvar.h>
#include <rex/logging.h>

#if FUTARI_RENUT_RENDERER
#include "renut_engine/gpu/banjo_renut_select.h"
#endif

// Dev aid: draw gameplay with a chosen Smoothing (0 Off, 1 On, 4 Sharp bilinear) without touching the save, for
// captures; also logs the gameplay blit's quad:
//   --futari_dev_smoothing=4
REXCVAR_DEFINE_STRING(futari_dev_smoothing, "", "Futari",
                      "Dev: the gameplay Smoothing, 0 Off, 1 On or 4 Sharp bilinear (empty: the save's)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {

using futari::smoothing::BlitDrawMode;
using futari::smoothing::FindSmoothingLabels;
using futari::smoothing::kDisplayBlockPointer;
using futari::smoothing::ParseDevSmoothing;
using futari::smoothing::SmoothingFallbackIndex;
using futari::smoothing::SmoothingIndex;
using futari::smoothing::SmoothingLabel;
using futari::smoothing::SmoothingLabelSet;
using futari::smoothing::SmoothingStore;

// The display block's Smoothing word.
constexpr uint32_t kDisplaySmoothing = 68;
// Settings: the current Picture preset index (0..7) and the presets, 44 bytes each, Smoothing in word 0.
constexpr uint32_t kPicturePresetIndex = 28;
constexpr uint32_t kPicturePresets = 1056;
constexpr uint32_t kPicturePresetSize = 44;
// sub_82185E80's flag for the Picture preset's defaults.
constexpr uint32_t kPictureDefaultsFlag = 0x4;
// The Picture page: its item vector (first, last), the cursor row and the settings object.
constexpr uint32_t kPageItemsFirst = 324;
constexpr uint32_t kPageItemsLast = 328;
constexpr uint32_t kPageCursor = 336;
constexpr uint32_t kPageSettings = 360;
constexpr uint32_t kPicturePresetLast = 7;
// The game's 80-byte menu item: id, type (3 = a list), value (a list's label index) and the list's label vector, whose
// first and last element sit at +12 and +16 of it. Smoothing is the list with id 0.
constexpr uint32_t kItemSize = 80;
constexpr uint32_t kItemId = 28;
constexpr uint32_t kItemType = 36;
constexpr uint32_t kItemValue = 40;
constexpr uint32_t kItemLabels = 56;
constexpr uint32_t kVectorFirst = 12;
constexpr uint32_t kVectorLast = 16;
constexpr uint32_t kSmoothingItemId = 0;
constexpr uint32_t kListItem = 3;
// A label is an item too: its std::string at +0 (short buffer +4, size +20, capacity +24, on the heap from
// capacity 16) and its own, empty, label vector.
constexpr uint32_t kStringBuffer = 4;
constexpr uint32_t kStringSize = 20;
constexpr uint32_t kStringCapacity = 24;
constexpr uint32_t kShortStringCapacity = 16;
// The stack frame the pushed label is copied into: the 80-byte copy at +112, above the push's own frame and its
// callee's parameter area.
constexpr uint32_t kPushFrame = 192;
constexpr uint32_t kPushCopyOffset = 112;
// The blit draws its argument's quad with sub_8206BF48(prim, decl, 4, vertices): 4 vertices of 16 bytes, big-endian
// f32 x, y, u, v (built by sub_821852C8 at its sp + 80).
constexpr uint32_t kBlitVertexCount = 4;
constexpr uint32_t kBlitVertexWords = 4;
using BlitQuad = std::array<uint32_t, kBlitVertexCount * kBlitVertexWords>;
// sub_82183C68 builds its VS matrix (c10..c13, uploaded once before its one draw) at its entry r1 - 160.
constexpr uint32_t kBlitMatrixBelowEntry = 160;

// The blit's vertex address and its entry r1 while sub_82183C68 runs on this thread; 0 otherwise.
thread_local uint32_t g_blit_vertices = 0;
thread_local uint32_t g_blit_entry_sp = 0;
// The quad last logged (raw words), per guest thread that draws the blit.
thread_local std::optional<BlitQuad> g_logged_quad;

// The Smoothing word Renut was last told (it starts with the game's shader, as for Off).
std::atomic<uint32_t> g_told_mode{futari::smoothing::kSmoothingOff};

const std::optional<uint32_t>& DevSmoothing() {
  static const std::optional<uint32_t> value = [] {
    const std::string text = REXCVAR_GET(futari_dev_smoothing);
    if (text.empty()) {
      return std::optional<uint32_t>{};
    }
    const std::optional<uint32_t> parsed = ParseDevSmoothing(text);
    if (!parsed) {
      REXLOG_WARN("futari_dev_smoothing = \"{}\" is not 0, 1 or 4; ignored", text);
    }
    return parsed;
  }();
  return value;
}

// The Smoothing item of a Picture page: the list with id 0, or 0 when there is none.
uint32_t FindSmoothingItem(uint8_t* base, uint32_t page) {
  const uint32_t last = REX_LOAD_U32(page + kPageItemsLast);
  for (uint32_t item = REX_LOAD_U32(page + kPageItemsFirst); item + kItemSize <= last; item += kItemSize) {
    if (REX_LOAD_U32(item + kItemId) == kSmoothingItemId && REX_LOAD_U32(item + kItemType) == kListItem) {
      return item;
    }
  }
  return 0;
}

uint32_t LabelCount(uint8_t* base, uint32_t item) {
  const uint32_t first = REX_LOAD_U32(item + kItemLabels + kVectorFirst);
  const uint32_t last = REX_LOAD_U32(item + kItemLabels + kVectorLast);
  return last >= first && (last - first) % kItemSize == 0 ? (last - first) / kItemSize : 0;
}

// Every label a short string with no labels of its own: what the push's copy and the in-place rename rely on.
bool LabelsPlain(uint8_t* base, uint32_t item, uint32_t count) {
  const uint32_t first = REX_LOAD_U32(item + kItemLabels + kVectorFirst);
  for (uint32_t label = first; label < first + count * kItemSize; label += kItemSize) {
    if (REX_LOAD_U32(label + kStringCapacity) >= kShortStringCapacity ||
        REX_LOAD_U32(label + kItemLabels + kVectorFirst) != REX_LOAD_U32(label + kItemLabels + kVectorLast)) {
      return false;
    }
  }
  return true;
}

// A plain label's text: its short-string buffer, at most the buffer's 15 bytes.
std::string LabelText(uint8_t* base, uint32_t label) {
  const uint32_t size = std::min(REX_LOAD_U32(label + kStringSize), kShortStringCapacity - 1);
  std::string text(size, '\0');
  for (uint32_t i = 0; i < size; ++i) {
    text[i] = char(REX_LOAD_U8(label + kStringBuffer + i));
  }
  return text;
}

// The item's label layout (count labels): the game's two, our three or anything else.
SmoothingLabelSet ItemLabels(uint8_t* base, uint32_t item, uint32_t count) {
  if (!LabelsPlain(base, item, count)) {
    return SmoothingLabelSet::kUnexpected;
  }
  const uint32_t first = REX_LOAD_U32(item + kItemLabels + kVectorFirst);
  const std::string label0 = count > 0 ? LabelText(base, first) : std::string();
  const std::string label1 = count > 1 ? LabelText(base, first + kItemSize) : std::string();
  return FindSmoothingLabels(count, true, label0, label1);
}

// Appends a copy of the game's "Off" (label 0) with the game's own push_back, from a copy on the guest stack; the
// copy owns nothing (a short string, an empty vector), so it is never destroyed.
void PushOffLabel(PPCContext& ctx, uint8_t* base, uint32_t item) {
  const uint32_t off = REX_LOAD_U32(item + kItemLabels + kVectorFirst);
  const uint32_t stack = ctx.r1.u32;
  const uint32_t frame = (stack - kPushFrame) & ~15u;
  const uint32_t copy = frame + kPushCopyOffset;
  for (uint32_t i = 0; i < kItemSize; i += 4) {
    REX_STORE_U32(copy + i, REX_LOAD_U32(off + i));
  }
  ctx.r1.u32 = frame;
  ctx.r3.u32 = item + kItemLabels;
  ctx.r4.u32 = copy;
  sub_82066888(ctx, base);
  ctx.r1.u32 = stack;
}

// Writes a label into a short string's buffer, NUL after it.
void RenameLabel(uint8_t* base, uint32_t label, std::string_view text) {
  for (uint32_t i = 0; i < text.size(); ++i) {
    REX_STORE_U8(label + kStringBuffer + i, uint8_t(text[i]));
  }
  REX_STORE_U8(label + kStringBuffer + text.size(), 0);
  REX_STORE_U32(label + kStringSize, uint32_t(text.size()));
}

// The Smoothing item after the game's load (value = the stored word): labels Smooth / Sharp / Off and the word's
// menu index. Any other label layout keeps the game's Off/On with a label index that stays below its count.
void LoadSmoothingItem(PPCContext& ctx, uint8_t* base, uint32_t item) {
  const uint32_t word = REX_LOAD_U32(item + kItemValue);
  uint32_t count = LabelCount(base, item);
  SmoothingLabelSet labels = ItemLabels(base, item, count);
  if (labels == SmoothingLabelSet::kGames) {
    PushOffLabel(ctx, base, item);
    count = LabelCount(base, item);
    labels = count == 3 ? SmoothingLabelSet::kOurs : SmoothingLabelSet::kUnexpected;
    if (labels == SmoothingLabelSet::kOurs) {
      const uint32_t first = REX_LOAD_U32(item + kItemLabels + kVectorFirst);
      for (uint32_t index = 0; index < 2; ++index) {
        if (const std::optional<std::string_view> label = SmoothingLabel(index)) {
          RenameLabel(base, first + index * kItemSize, *label);
        }
      }
    }
  }
  if (labels == SmoothingLabelSet::kOurs) {
    REX_STORE_U32(item + kItemValue, SmoothingIndex(word));
    return;
  }
  REX_STORE_U32(item + kItemValue, SmoothingFallbackIndex(word, count));
  static bool warned = false;
  if (!warned) {
    warned = true;
    REXLOG_WARN("Smoothing menu: unexpected labels ({}); the item keeps the game's Off/On", count);
  }
}

}  // namespace

namespace futari::smoothing {

void ApplyDevSmoothing(uint8_t* base, uint32_t display_block) {
  const std::optional<uint32_t>& mode = DevSmoothing();
  if (!mode) {
    return;
  }
  REX_STORE_U32(display_block + kDisplaySmoothing, *mode);
  static bool logged = false;
  if (!logged) {
    logged = true;
    REXLOG_INFO("Smoothing {} from futari_dev_smoothing; the save keeps its own", *mode);
  }
}

void NoteBlitDraw(uint8_t* base, uint32_t vertices, uint32_t count) {
  if (g_blit_vertices == 0 || vertices != g_blit_vertices || count != kBlitVertexCount || !DevSmoothing()) {
    return;
  }
  BlitQuad quad;
  for (uint32_t i = 0; i < quad.size(); ++i) {
    quad[i] = REX_LOAD_U32(vertices + 4 * i);
  }
  if (g_logged_quad == quad) {
    return;
  }
  g_logged_quad = quad;
  const auto f = [&quad](uint32_t i) { return std::bit_cast<float>(quad[i]); };
  REXLOG_INFO("Blit quad (x, y, u, v): ({}, {}, {}, {}) ({}, {}, {}, {}) ({}, {}, {}, {}) ({}, {}, {}, {})", f(0), f(1),
              f(2), f(3), f(4), f(5), f(6), f(7), f(8), f(9), f(10), f(11), f(12), f(13), f(14), f(15));
}

uint32_t BlitDrawMatrix(uint32_t vertices, uint32_t count) {
  if (g_blit_vertices == 0 || vertices != g_blit_vertices || count != kBlitVertexCount) {
    return 0;
  }
  return g_blit_entry_sp - kBlitMatrixBelowEntry;
}

}  // namespace futari::smoothing

// The gameplay blit (r4 = its quad): Sharp runs as On, restored after unless the game changed it meanwhile.
REX_EXTERN(__imp__sub_82183C68);
extern "C" REX_FUNC(sub_82183C68) {
  const uint32_t block = REX_LOAD_U32(kDisplayBlockPointer);
  const uint32_t mode = block != 0 ? REX_LOAD_U32(block + kDisplaySmoothing) : 0;
#if FUTARI_RENUT_RENDERER
  if (block != 0 && banjo::renut_gpu::Active()) {
    if (g_told_mode.exchange(mode, std::memory_order_relaxed) != mode) {
      renut::gpu::SetBlitSmoothing(mode);
    }
  }
#endif
  const uint32_t draw_mode = BlitDrawMode(mode);
  const bool patched = block != 0 && draw_mode != mode;
  if (patched) {
    REX_STORE_U32(block + kDisplaySmoothing, draw_mode);
  }
  g_blit_vertices = ctx.r4.u32;
  g_blit_entry_sp = ctx.r1.u32;
  __imp__sub_82183C68(ctx, base);
  g_blit_vertices = 0;
  g_blit_entry_sp = 0;
  if (patched && REX_LOAD_U32(block + kDisplaySmoothing) == draw_mode) {
    REX_STORE_U32(block + kDisplaySmoothing, mode);
  }
}

// Settings defaults (r3 = settings, r4 = the target or 0 for r3, r5 = flags). Flag 0x4 writes the current Picture
// preset's words with Smoothing = 1; with Renut the default is Sharp bilinear instead. A loaded save never comes here.
REX_EXTERN(__imp__sub_82185E80);
extern "C" REX_FUNC(sub_82185E80) {
  const uint32_t settings = ctx.r3.u32;
  const uint32_t target_arg = ctx.r4.u32;
  const uint32_t flags = ctx.r5.u32;
  __imp__sub_82185E80(ctx, base);
#if FUTARI_RENUT_RENDERER
  if ((flags & kPictureDefaultsFlag) == 0 || !banjo::renut_gpu::Active()) {
    return;
  }
  const uint32_t target = target_arg != 0 ? target_arg : settings;
  // The original clamped the preset index to 0..7 and stored it back; clamped again so no caller can move the write.
  const uint32_t index = std::min(REX_LOAD_U32(target + kPicturePresetIndex), kPicturePresetLast);
  const uint32_t word = target + kPicturePresets + kPicturePresetSize * index;
  if (REX_LOAD_U32(word) != futari::smoothing::kSmoothingOn) {
    return;
  }
  REX_STORE_U32(word, futari::smoothing::ResetSmoothing(true));
  static bool logged = false;
  if (!logged) {
    logged = true;
    REXLOG_INFO("Smoothing default: Sharp bilinear with Renut");
  }
#else
  (void)settings;
  (void)target_arg;
  (void)flags;
#endif
}

// Picture page item load (r3 = the page): the game puts the stored word into the Smoothing item's value; the item
// gets the labels Smooth / Sharp / Off (its third label pushed once; a reload already has it) and the word's index.
REX_EXTERN(__imp__sub_8219D840);
extern "C" REX_FUNC(sub_8219D840) {
  const uint32_t page = ctx.r3.u32;
  __imp__sub_8219D840(ctx, base);
  const uint32_t item = FindSmoothingItem(base, page);
  if (item != 0) {
    LoadSmoothingItem(ctx, base, item);
  }
}

// Picture page change (r3 = the page, r4 = {new value, old value}, the high word stored for the cursor item): for the
// relabelled Smoothing item the new value is a menu index, turned into its word here, the stored word kept while it
// already shows as that index (the game stores the row once without input after each load).
REX_EXTERN(__imp__sub_8219F980);
extern "C" REX_FUNC(sub_8219F980) {
  const uint32_t page = ctx.r3.u32;
  const uint32_t first = REX_LOAD_U32(page + kPageItemsFirst);
  const uint32_t count = (REX_LOAD_U32(page + kPageItemsLast) - first) / kItemSize;
  const uint32_t cursor = REX_LOAD_U32(page + kPageCursor);
  const uint32_t item = first + cursor * kItemSize;
  if (cursor < count && REX_LOAD_U32(item + kItemId) == kSmoothingItemId &&
      REX_LOAD_U32(item + kItemType) == kListItem &&
      ItemLabels(base, item, LabelCount(base, item)) == SmoothingLabelSet::kOurs) {
    const uint32_t settings = REX_LOAD_U32(page + kPageSettings);
    const int32_t index = static_cast<int32_t>(REX_LOAD_U32(settings + kPicturePresetIndex));
    const uint32_t preset = static_cast<uint32_t>(std::clamp<int32_t>(index, 0, kPicturePresetLast));
    const uint32_t stored = REX_LOAD_U32(settings + kPicturePresets + kPicturePresetSize * preset);
    const uint32_t value = SmoothingStore(static_cast<uint32_t>(ctx.r4.u64 >> 32), stored);
    ctx.r4.u64 = (uint64_t(value) << 32) | ctx.r4.u32;
  }
  __imp__sub_8219F980(ctx, base);
}
