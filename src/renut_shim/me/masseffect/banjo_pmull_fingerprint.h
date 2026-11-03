// futari_recomp shim for futari-nx's "me/masseffect/banjo_pmull_fingerprint.h" (banjo-nx 2026-10-15; the original
// lives in cerroverb/futari-linux app/src/me/masseffect/banjo_pmull_fingerprint.h).
//
// LICENCE: the polynomial constants, MulXPow, XPow, BarrettMu, Mix and Reference below are copied from that original
// and remain under its licence, GPL-3.0 (cerroverb/futari-linux, from ChanseyIsTheBest/futari-nx and
// masterspike52/reNut); the surrounding text and the scalar Fingerprint alias are futari_recomp's.
//
// The original: a 64-bit CRC (polynomial x^64 + 0x42F0E1EBA9EA3693, ECMA-182) folded with ARMv8 PMULL for the ring
// thread's whole-range fingerprints, with a bit-by-bit scalar Reference for its self-test. Renut uses it in
// src/renut_engine/gpu/banjo_renut_tuning.cpp: BANJO_PMULL_FINGERPRINT (#if, lines 564 and 1327), kAvailable (1432),
// Fingerprint and Reference (575, 1328).
//
// Here the NEON/PMULL path is dropped: BANJO_PMULL_FINGERPRINT is 0 and kAvailable false on every build, Windows and
// macOS (Apple Silicon has PMULL but the hash is an optimisation, not a result), so Renut's PmullHash() stays on XXH3
// and BeforeDeviceCreated() never runs the self-test. Fingerprint is still defined, as the scalar Reference, so the
// name resolves if a future build turns the macro on by hand; the two agree bit for bit by construction.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#define BANJO_PMULL_FINGERPRINT 0

namespace banjo::pmull_fingerprint {

constexpr bool kAvailable = BANJO_PMULL_FINGERPRINT != 0;
constexpr uint64_t kPoly = 0x42F0E1EBA9EA3693ull;  // P(x) - x^64

// r * x^n mod P, bit by bit (compile time and the reference).
constexpr uint64_t MulXPow(uint64_t r, unsigned n) {
  for (unsigned i = 0; i < n; ++i) {
    const bool carry = (r >> 63) != 0;
    r <<= 1;
    if (carry) r ^= kPoly;
  }
  return r;
}
constexpr uint64_t XPow(unsigned n) { return MulXPow(1, n); }

// The low 64 coefficients of floor(x^128 / P) (its x^64 coefficient is 1): Barrett's constant.
constexpr uint64_t BarrettMu() {
  uint64_t q = 0, r = 0;
  for (int i = 128; i >= 0; --i) {
    const bool top = (r >> 63) != 0;
    r = (r << 1) | (i == 128 ? 1u : 0u);
    if (top) {
      r ^= kPoly;
      if (i < 64) q |= uint64_t(1) << i;
    }
  }
  return q;
}

constexpr uint64_t kMu = BarrettMu();
// Folding by k blocks: x^(128k) mod P for the low half, x^(128k + 64) mod P for the high half.
constexpr uint64_t kFold1Lo = XPow(128), kFold1Hi = XPow(192);
constexpr uint64_t kFold2Lo = XPow(256), kFold2Hi = XPow(320);
constexpr uint64_t kFold3Lo = XPow(384), kFold3Hi = XPow(448);
constexpr uint64_t kFold4Lo = XPow(512), kFold4Hi = XPow(576);

// The bijective final mix (fixed length): distinct residues give distinct fingerprints.
inline uint64_t Mix(uint64_t residue, uint64_t bytes) {
  uint64_t h = residue ^ (bytes * 0x9E3779B97F4A7C15ull);
  h *= 0xFF51AFD7ED558CCDull;
  h ^= h >> 33;
  h *= 0xC4CEB9FE1A85EC53ull;
  h ^= h >> 33;
  return h;
}

// The bit-by-bit reference: the residue block by block (16-byte little-endian blocks, the seed added to the first,
// the last zero-padded), then Mix.
inline uint64_t Reference(const uint8_t* data, size_t bytes, uint64_t seed) {
  uint64_t s = 0;
  for (size_t at = 0; at < bytes; at += 16) {
    uint8_t block[16] = {};
    std::memcpy(block, data + at, bytes - at < 16 ? bytes - at : 16);
    uint64_t lo, hi;
    std::memcpy(&lo, block, 8);
    std::memcpy(&hi, block + 8, 8);
    if (at == 0) lo ^= seed;
    s = MulXPow(s, 128) ^ lo ^ MulXPow(hi, 64);
  }
  if (bytes == 0) s = seed;
  return Mix(s, bytes);
}

// The portable fingerprint: the same value the original's PMULL path computes (it is checked against Reference by
// Renut's PmullSelfTest), by the scalar route. Not selected on this build (kAvailable is false).
inline uint64_t Fingerprint(const uint8_t* data, size_t bytes, uint64_t seed) { return Reference(data, bytes, seed); }

}  // namespace banjo::pmull_fingerprint
