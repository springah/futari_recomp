// Standalone test of src/renut_engine/gpu/d3d/edram_tiles.h (pure std): the EDRAM tiles a surface takes
// (ceil(W/80) * ceil(H/16) at 32 bpp, twice that at 64 bpp, more with MSAA) and which surfaces are larger than the
// console's 2048 tiles, and which reserve the free run found for them. Locally with any C++20 compiler:
//
//   clang++ -std=c++20 -Wall -Wextra -I src tests/edram_tiles_test.cpp -o edram_tiles_test
#include "renut_engine/gpu/d3d/edram_tiles.h"

#include <cstdint>
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

using renut::gpu::EdramOverflows;
using renut::gpu::EdramReserves;
using renut::gpu::EdramTileCount;
using renut::gpu::kEdramUsableTiles;

constexpr uint32_t kArgb = 0x18280186u;  // D3DFMT_A8R8G8B8 (low bits 6), a back buffer's format
constexpr uint32_t kDepth = 22;          // D24S8: 32 bpp too
constexpr uint32_t k64Bit[] = {21, 32, 37};

void Sizes32Bit() {
  Check(EdramTileCount(1280, 720, kArgb, 0) == 720, "1280x720: 16 x 45 tiles");
  Check(EdramTileCount(1920, 1080, kArgb, 0) == 1632, "1920x1080: 24 x 68 tiles");
  Check(EdramTileCount(1920, 1200, kArgb, 0) == 1800, "1920x1200: 24 x 75 tiles");
  Check(EdramTileCount(2560, 1440, kArgb, 0) == 2880, "2560x1440: 32 x 90 tiles");
  Check(EdramTileCount(3840, 2160, kArgb, 0) == 6480, "3840x2160: 48 x 135 tiles");
  Check(EdramTileCount(8192, 8192, kArgb, 0) == 52736, "8192x8192: 103 x 512 tiles");
  Check(EdramTileCount(1080, 1920, kArgb, 0) == 1680, "portrait 1080x1920: 14 x 120 tiles");
  Check(EdramTileCount(1920, 1080, kDepth, 0) == 1632, "a depth surface counts as 32 bpp");
  Check(EdramTileCount(81, 17, kArgb, 0) == 4, "partial tiles round up per axis");
}

void Sizes64Bit() {
  for (const uint32_t f : k64Bit) {
    Check(EdramTileCount(1280, 720, f, 0) == 1440, "64 bpp: twice the tiles");
    Check(EdramTileCount(1920, 1080, f, 0) == 3264, "64 bpp 1920x1080");
  }
  Check(EdramTileCount(1280, 720, 0x40u | 32, 0) == 1440, "only the low 6 format bits count");
}

void Multisample() {
  Check(EdramTileCount(1280, 720, kArgb, 1) == 1440, "2x MSAA doubles the height");
  Check(EdramTileCount(1280, 720, kArgb, 2) == 2880, "4x MSAA doubles both axes");
  // The largest frame at 64 bpp and 4x MSAA stays within 32 bits.
  Check(EdramTileCount(8192, 8192, 32, 2) == 419840, "8192x8192 64 bpp 4x: 205 x 1024 x 2 tiles");
}

void Overflow() {
  Check(kEdramUsableTiles == 2048, "the console's 2048 tiles");
  Check(!EdramOverflows(EdramTileCount(1280, 720, kArgb, 0)), "1280x720 fits");
  Check(!EdramOverflows(EdramTileCount(1920, 1080, kArgb, 0)), "1920x1080 fits");
  Check(!EdramOverflows(EdramTileCount(1920, 1200, kArgb, 0)), "1920x1200 fits");
  Check(!EdramOverflows(EdramTileCount(1080, 1920, kArgb, 0)), "portrait 1080x1920 fits");
  Check(EdramTileCount(2560, 1024, kArgb, 0) == 2048, "2560x1024: exactly 2048 tiles");
  Check(!EdramOverflows(EdramTileCount(2560, 1024, kArgb, 0)), "exactly 2048 tiles fits");
  Check(EdramOverflows(EdramTileCount(2560, 1025, kArgb, 0)), "one tile row more does not");
  Check(EdramOverflows(EdramTileCount(2561, 1024, kArgb, 0)), "one tile column more does not");
  Check(EdramOverflows(EdramTileCount(2560, 1440, kArgb, 0)), "2560x1440 does not fit");
  Check(EdramOverflows(EdramTileCount(3840, 2160, kArgb, 0)), "3840x2160 does not fit");
  Check(EdramOverflows(EdramTileCount(8192, 8192, kArgb, 0)), "8192x8192 does not fit");
  Check(EdramOverflows(EdramTileCount(1920, 1080, 32, 0)), "1920x1080 at 64 bpp does not fit");
  Check(EdramOverflows(EdramTileCount(1280, 720, kArgb, 2)), "1280x720 at 4x MSAA does not fit");
}

// A surface without parameters reserves the run the allocator found (its base, -1 for none) only when that run lies
// inside the console's 2048 tiles; otherwise it takes the overflow path (base 0, nothing reserved).
void Reserve() {
  const uint32_t gameplay = EdramTileCount(640, 512, kArgb, 0);
  Check(gameplay == 256, "gameplay's 640x512: 8 x 32 tiles");
  Check(EdramReserves(gameplay, 1632), "1920x1080 frame: 640x512 fits after it at base 1632");
  Check(EdramReserves(gameplay, 1792), "a run ending at exactly 2048 tiles");
  Check(!EdramOverflows(gameplay), "640x512 fits on its own");
  Check(!EdramReserves(gameplay, -1), "1920x1200 frame (1800 tiles): no free run left, overflow path");
  Check(!EdramReserves(gameplay, 1793), "a run past 2048 tiles: overflow path");
  Check(!EdramReserves(gameplay, 0x7FFFFFFF), "a base far past EDRAM does not wrap into one that fits");
  Check(EdramReserves(2048, 0), "a surface of exactly 2048 tiles at base 0");
  Check(!EdramReserves(2049, 0), "a surface larger than the EDRAM never reserves");
  Check(!EdramReserves(UINT32_MAX, 0), "a saturated count never reserves");
}

// Surfaces past the frame cap: the count must not wrap around 32 bits into a size that fits.
void NoWrap() {
  Check(EdramTileCount(8192, 32768, 32, 2) == 1679360, "8192x32768 64 bpp 4x: 205 x 4096 x 2 tiles");
  Check(EdramOverflows(EdramTileCount(8192, 32768, 32, 2)), "8192x32768 64 bpp 4x does not fit");
  Check(EdramTileCount(16384, 16384, 32, 2) == 1679360, "16384x16384 64 bpp 4x: 410 x 2048 x 2 tiles");
  Check(EdramOverflows(EdramTileCount(16384, 16384, 32, 2)), "16384x16384 64 bpp 4x does not fit");
  Check(EdramTileCount(0xFFFFFFFFu, 480, kArgb, 0) == 1610612760u, "the largest width: 53687092 x 30 tiles");
  Check(EdramOverflows(EdramTileCount(0xFFFFFFFFu, 480, kArgb, 0)), "the largest width does not fit");
  Check(EdramTileCount(640, 0xFFFFFFF1u, kArgb, 0) == 2147483648u, "the largest heights: 8 x 268435456 tiles");
  Check(EdramOverflows(EdramTileCount(640, 0xFFFFFFF1u, kArgb, 0)), "the largest heights do not fit");
  Check(EdramTileCount(0xFFFFFFFFu, 0xFFFFFFFFu, 32, 2) == UINT32_MAX, "a count past 32 bits saturates");
}

}  // namespace

int main() {
  Sizes32Bit();
  Sizes64Bit();
  Multisample();
  Overflow();
  Reserve();
  NoWrap();
  std::printf("edram_tiles_test: %d checks, %d failed\n", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
