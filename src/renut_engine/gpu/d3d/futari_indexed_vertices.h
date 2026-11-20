// futari_recomp (2026-11-07): the scratch layout of D3DDevice_BeginIndexedVertices (hooks/draw.cpp). Pure std, so
// tests/indexed_vertices_test.cpp checks it on its own.
#pragma once

#include <cstdint>

namespace renut::gpu {

// The vertices first, then the indices from the next 16 bytes (the XDK's original allocates both 16-aligned).
// Vertex bytes = NumVertices * Stride; an index is four bytes with IndexFormat bit 2 (D3DFMT_INDEX32), else two,
// INDEX16 rounded up to whole dwords ((IndexCount + 1) / 2 of them, as the original).
// In 64 bits: the caller rejects a layout larger than its scratch.
struct IndexedVerticesLayout {
  uint64_t vertex_bytes = 0;
  uint64_t index_offset = 0;
  uint64_t index_bytes = 0;
  uint64_t total = 0;
};

constexpr IndexedVerticesLayout IndexedVerticesScratch(uint32_t num_vertices, uint32_t stride, uint32_t index_count,
                                                       uint32_t index_format) {
  IndexedVerticesLayout l;
  l.vertex_bytes = uint64_t(num_vertices) * stride;
  l.index_bytes = (index_format & 4) ? uint64_t(index_count) * 4 : (uint64_t(index_count) + 1) / 2 * 4;
  l.index_offset = (l.vertex_bytes + 15) & ~uint64_t(15);
  l.total = l.index_offset + l.index_bytes;
  return l;
}

} // namespace renut::gpu
