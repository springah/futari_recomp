// Standalone test of src/renut_engine/gpu/d3d/front_buffer_block.h (pure std): whether a resized automatic front
// buffer keeps its data block or needs a larger one. Locally with any C++20 compiler:
//
//   clang++ -std=c++20 -Wall -Wextra -I src tests/front_buffer_block_test.cpp -o front_buffer_block_test
#include "renut_engine/gpu/d3d/front_buffer_block.h"

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

using renut::gpu::FrontBufferBlock;

constexpr uint32_t k1080p = 1920 * 1080 * 4;
constexpr uint32_t k720p = 1280 * 720 * 4;
constexpr uint32_t k2160p = 3840 * 2160 * 4;

void EqualNeedReuses() {
  const auto plan = FrontBufferBlock(k1080p, k1080p);
  Check(!plan.grow, "the same size keeps the block");
  Check(plan.capacity == k1080p, "the same size keeps the capacity");
}

void ShrinkReusesAndKeepsTheCapacity() {
  const auto plan = FrontBufferBlock(k1080p, k720p);
  Check(!plan.grow, "a smaller size keeps the block");
  Check(plan.capacity == k1080p, "a smaller size keeps the larger capacity");
  const auto by_one = FrontBufferBlock(k1080p, k1080p - 1);
  Check(!by_one.grow && by_one.capacity == k1080p, "one byte less keeps the block");
}

void GrowAllocatesTheNeed() {
  const auto plan = FrontBufferBlock(k1080p, k2160p);
  Check(plan.grow, "a larger size needs a new block");
  Check(plan.capacity == k2160p, "the new block's capacity is the need");
  const auto by_one = FrontBufferBlock(k1080p, k1080p + 1);
  Check(by_one.grow && by_one.capacity == k1080p + 1, "one byte more needs a new block");
  // Shrink, then grow back to the old size: the block kept its capacity, so no allocation.
  const auto back = FrontBufferBlock(FrontBufferBlock(k1080p, k720p).capacity, k1080p);
  Check(!back.grow && back.capacity == k1080p, "growing back within the kept capacity reuses");
}

void ZeroCapacityGrows() {
  const auto plan = FrontBufferBlock(0, k720p);
  Check(plan.grow, "no block yet needs one");
  Check(plan.capacity == k720p, "its capacity is the need");
}

void ZeroNeedReuses() {
  const auto plan = FrontBufferBlock(k720p, 0);
  Check(!plan.grow, "nothing to hold keeps the block");
  Check(plan.capacity == k720p, "nothing to hold keeps the capacity");
  const auto none = FrontBufferBlock(0, 0);
  Check(!none.grow && none.capacity == 0, "no block and nothing to hold allocates nothing");
}

}  // namespace

int main() {
  EqualNeedReuses();
  ShrinkReusesAndKeepsTheCapacity();
  GrowAllocatesTheNeed();
  ZeroCapacityGrows();
  ZeroNeedReuses();
  std::printf("front_buffer_block_test: %d checks, %d failed\n", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
