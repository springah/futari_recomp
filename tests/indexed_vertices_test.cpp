// Standalone test of src/renut_engine/gpu/d3d/futari_indexed_vertices.h (pure std): the scratch layout Renut's
// D3DDevice_BeginIndexedVertices hands out, vertices first and the indices after them, as the XDK's original sizes
// them (vertex bytes = NumVertices * Stride; index size from IndexFormat bit 2, INDEX16 in whole dwords).
// Locally with any C++20 compiler:
//
//   clang++ -std=c++20 -Wall -Wextra -I src tests/indexed_vertices_test.cpp -o indexed_vertices_test
#include "renut_engine/gpu/d3d/futari_indexed_vertices.h"

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

using renut::gpu::IndexedVerticesScratch;

void ScreenOverlayFrame() {
  // The Screen page's frame: 8 vertices, 24 INDEX16 indices (format 1); a 20-byte stride, say.
  const auto l = IndexedVerticesScratch(8, 20, 24, 1);
  Check(l.vertex_bytes == 160, "vertex bytes = NumVertices * Stride");
  Check(l.index_bytes == 48, "INDEX16: two bytes an index");
  Check(l.index_offset == 160, "indices right after 16-aligned vertices");
  Check(l.total == 208, "total = index offset + index bytes");
}

void IndicesStartAligned() {
  const auto l = IndexedVerticesScratch(3, 12, 3, 1);
  Check(l.vertex_bytes == 36, "36 vertex bytes");
  Check(l.index_offset == 48, "index offset rounded up to 16");
  Check(l.total == 56, "total from the aligned offset");
}

void OddIndex16() {
  // The original allocates INDEX16 indices in whole dwords: (IndexCount + 1) / 2 of them.
  const auto one = IndexedVerticesScratch(1, 16, 1, 1);
  Check(one.index_bytes == 4, "INDEX16: one index rounds up to a dword");
  Check(one.index_offset == 16, "16 vertex bytes, already aligned");
  Check(one.total == 20, "total for one INDEX16 index");
  const auto three = IndexedVerticesScratch(3, 12, 3, 1);
  Check(three.index_bytes == 8, "INDEX16: three indices round up to two dwords");
  Check(three.total == 56, "total from the aligned offset, rounded indices");
}

void Index32() {
  // D3DFMT_INDEX32 has bit 2 set (6 on Xenon).
  const auto l = IndexedVerticesScratch(4, 16, 6, 6);
  Check(l.index_bytes == 24, "INDEX32: four bytes an index");
  Check(l.index_offset == 64, "64 vertex bytes, already aligned");
  Check(l.total == 88, "total for INDEX32");
}

void NoOverflow() {
  const auto l = IndexedVerticesScratch(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 6);
  Check(l.vertex_bytes == uint64_t(0xFFFFFFFFu) * 0xFFFFFFFFu, "vertex bytes in 64 bits");
  Check(l.total > uint64_t(0xFFFFFFFFu), "a huge request stays huge (the hook rejects it)");
}

void Empty() {
  const auto l = IndexedVerticesScratch(0, 16, 0, 1);
  Check(l.vertex_bytes == 0 && l.index_bytes == 0 && l.total == 0, "nothing asked, nothing laid out");
}

}  // namespace

int main() {
  ScreenOverlayFrame();
  IndicesStartAligned();
  OddIndex16();
  Index32();
  NoOverflow();
  Empty();
  std::printf("indexed_vertices_test: %d checks, %d failed\n", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
