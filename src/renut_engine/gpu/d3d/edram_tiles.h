// futari_recomp (2026-11-07): the EDRAM tiles a surface takes, out of hooks/resource.cpp so
// tests/edram_tiles_test.cpp checks it on its own (pure std).
#pragma once

#include <cstdint>

namespace renut::gpu {

// The console's 10 MiB of EDRAM in 5120-byte tiles: no surface base + tiles goes past it.
constexpr uint32_t kEdramUsableTiles = 0x800;

// The EDRAM tiles a surface takes (sub_8231A808): 80x16-pixel tiles at 32 bpp, twice as many for the 64-bit formats
// (21, 32, 37); 2x MSAA doubles the height, 4x the width as well. Counted in 64 bits and saturated at UINT32_MAX, so a
// surface far past EDRAM can't wrap into a count that fits.
constexpr uint32_t EdramTileCount(uint32_t width, uint32_t height, uint32_t format, uint32_t multisample) {
  const uint32_t f = format & 0x3F;
  uint64_t w = width, h = height;
  if (int32_t(multisample) >= 1)
    h = 2 * h;
  if (multisample == 2)
    w = 2 * w;
  const uint64_t tiles_per_block = (f == 21 || f == 32 || f == 37) ? 2 : 1;
  const uint64_t tiles = ((h + 15) / 16) * ((w + 79) / 80) * tiles_per_block;
  return tiles > UINT32_MAX ? UINT32_MAX : uint32_t(tiles);
}

// A surface no console could hold. Without explicit parameters it gets base 0 and reserves nothing: host targets are
// sized from the surface, never from EDRAM.
constexpr bool EdramOverflows(uint32_t tiles) {
  return tiles > kEdramUsableTiles;
}

// Whether a surface without parameters reserves the free run the allocator found for it (its base, or -1 for none):
// only a run inside the console's EDRAM. One that fits on its own but finds no run left (a large frame took the room)
// takes the overflow path as well: base 0, nothing reserved.
constexpr bool EdramReserves(uint32_t tiles, int32_t run_base) {
  return !EdramOverflows(tiles) && run_base >= 0 && uint64_t(uint32_t(run_base)) + tiles <= kEdramUsableTiles;
}

} // namespace renut::gpu
