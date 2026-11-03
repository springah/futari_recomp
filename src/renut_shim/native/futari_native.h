// futari_recomp shim for futari-nx's "native/futari_native.h" (GPL-3; the original, 245 lines, lives in
// cerroverb/futari-linux app/src/native/futari_native.h). Written from scratch for futari_recomp (BSD-3-Clause).
//
// The original is the framework of futari-nx's native replacements of hot guest functions (Site / Mode / Verify, the
// Direct3D census, WaitD3DGpuProgress, FormsSelfCheck, the codegen checks). futari_recomp has NO natives: nothing here
// decides a mode, verifies, or reports. Only what Renut's sources reference is provided:
//   - futari::native::CodeHash          src/renut_engine/futari_renut_vblank.cpp:99  (the vblank handler's code check)
//   - futari::native::CodegenChecksReport  src/renut_engine/futari_renut_shim.cpp:83 (banjo::CodegenChecksReport)
// plus Host/Contiguous, which CodeHash is written in terms of. CodeHash keeps the original's exact arithmetic (FNV-1a 64
// over the instruction words as stored, big-endian bytes) so the hashes Renut carries (kVblankHandlerHash) still match.
#pragma once

#include <cstddef>
#include <cstdint>

namespace futari::native {

// Host pointer of a guest virtual address. The original added 0x1000 for addresses >= 0xE0000000 on futari-nx's PC
// build (its physical views sit one page higher); futari_recomp's memory has no such offset and Renut's only caller
// hashes code at 0x82xxxxxx, so this is the plain translation.
inline uint8_t* Host(uint8_t* base, uint32_t address) { return base + address; }
inline const uint8_t* Host(const uint8_t* base, uint32_t address) { return base + address; }

// A guest range [address, address + size) that one host pointer covers (no crossing into the physical views).
inline bool Contiguous(uint32_t address, uint32_t size) {
  const uint64_t end = uint64_t(address) + size;
  return end <= 0x100000000ull && (address >= 0xE0000000u || end <= 0xE0000000u);
}

// FNV-1a 64 over the guest instruction words as stored (big-endian bytes). Same constants and order as the original.
inline uint64_t CodeHash(const uint8_t* base, uint32_t address, uint32_t words) {
  uint64_t h = 0xcbf29ce484222325ull;
  const uint8_t* p = Host(base, address);
  for (uint32_t i = 0; i < words * 4; ++i) {
    h = (h ^ p[i]) * 0x100000001b3ull;
  }
  return h;
}

// futari-nx's post-codegen self-checks (its futari_codegen_checks.cpp). futari_recomp runs no such pass: nothing to
// initialise and nothing to report with the renderer's 10 s report.
inline void CodegenChecksInit() {}
inline void CodegenChecksReport(int /*seconds*/) {}

}  // namespace futari::native
