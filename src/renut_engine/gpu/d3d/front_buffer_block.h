// futari_recomp (2026-11-10): whether a resized automatic front buffer keeps its data block, out of hooks/device.cpp
// so tests/front_buffer_block_test.cpp checks it on its own (pure std).
#pragma once

#include <cstdint>

namespace renut::gpu {

struct FrontBufferBlockPlan {
  bool grow = false;      // allocate a new block of 'capacity' bytes (then free the old one)
  uint32_t capacity = 0;  // the block's size after the resize
};

// A block of 'capacity' bytes holds any size up to it, so shrinking never allocates and keeps the larger block for a
// later growth back; only a need past the capacity allocates, exactly the need.
constexpr FrontBufferBlockPlan FrontBufferBlock(uint32_t capacity, uint32_t need) {
  if (need <= capacity)
    return {false, capacity};
  return {true, need};
}

}  // namespace renut::gpu
