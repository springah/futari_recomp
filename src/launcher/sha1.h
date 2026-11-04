/**
 * @file        sha1.h
 * @brief       SHA-1 for the console-install hash tree (game_source.cpp). futari_recomp (BSD 3-Clause), 2026-11-03.
 *
 * The SDK vendors SHA-256 only; the Xbox 360's SVOD containers chain SHA-1 digests. Straight FIPS 180-1, no table.
 */
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace futari::launcher {

class Sha1 {
 public:
  using Digest = std::array<uint8_t, 20>;

  Sha1() { Reset(); }

  void Reset() {
    h_ = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    length_ = 0;
    buffered_ = 0;
  }

  void Update(const uint8_t* data, size_t size) {
    length_ += size;
    while (size > 0) {
      const size_t take = std::min(size, size_t(64) - buffered_);
      std::memcpy(buffer_ + buffered_, data, take);
      buffered_ += take;
      data += take;
      size -= take;
      if (buffered_ == 64) {
        Block(buffer_);
        buffered_ = 0;
      }
    }
  }

  Digest Finish() {
    const uint64_t bits = length_ * 8;
    const uint8_t one = 0x80;
    Update(&one, 1);
    const uint8_t zero = 0;
    while (buffered_ != 56) {
      Update(&zero, 1);
    }
    uint8_t len[8];
    for (int i = 0; i < 8; ++i) {
      len[i] = uint8_t(bits >> (56 - 8 * i));
    }
    Update(len, 8);
    Digest out;
    for (int i = 0; i < 5; ++i) {
      out[i * 4 + 0] = uint8_t(h_[i] >> 24);
      out[i * 4 + 1] = uint8_t(h_[i] >> 16);
      out[i * 4 + 2] = uint8_t(h_[i] >> 8);
      out[i * 4 + 3] = uint8_t(h_[i]);
    }
    return out;
  }

  static Digest Of(const uint8_t* data, size_t size) {
    Sha1 s;
    s.Update(data, size);
    return s.Finish();
  }

 private:
  static uint32_t Rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

  void Block(const uint8_t* p) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
      w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) | (uint32_t(p[i * 4 + 2]) << 8) |
             uint32_t(p[i * 4 + 3]);
    }
    for (int i = 16; i < 80; ++i) {
      w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4];
    for (int i = 0; i < 80; ++i) {
      uint32_t f, k;
      if (i < 20) {
        f = (b & c) | (~b & d);
        k = 0x5A827999u;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1u;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDCu;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6u;
      }
      const uint32_t t = Rol(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = Rol(b, 30);
      b = a;
      a = t;
    }
    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
    h_[4] += e;
  }

  std::array<uint32_t, 5> h_;
  uint64_t length_ = 0;
  uint8_t buffer_[64];
  size_t buffered_ = 0;
};

}  // namespace futari::launcher
