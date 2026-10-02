// Copyright 2026
#ifndef FPNGE_INTERNAL_CRC_X86_H_
#define FPNGE_INTERNAL_CRC_X86_H_

#include <cstddef>
#include <cstdint>
#include <wmmintrin.h>

namespace fpnge_internal {

class Crc32X86 {
 public:
  Crc32X86()
      : x0_(_mm_cvtsi32_si128(0x9db42487)),
        x1_(_mm_setzero_si128()),
        x2_(_mm_setzero_si128()),
        x3_(_mm_setzero_si128()) {}

  size_t update(const unsigned char* data, size_t len) {
    const size_t amount = len & ~size_t{63};
    for (size_t i = 0; i < amount; i += 64) {
      x0_ = Fold(x0_, _mm_loadu_si128(
                          reinterpret_cast<const __m128i*>(data + i)));
      x1_ = Fold(x1_, _mm_loadu_si128(
                          reinterpret_cast<const __m128i*>(data + i + 0x10)));
      x2_ = Fold(x2_, _mm_loadu_si128(
                          reinterpret_cast<const __m128i*>(data + i + 0x20)));
      x3_ = Fold(x3_, _mm_loadu_si128(
                          reinterpret_cast<const __m128i*>(data + i + 0x30)));
    }
    return amount;
  }

  uint32_t update_final(const unsigned char* data, size_t len) {
    if (len >= 64) {
      update(data, len);
      data += len & ~size_t{63};
      len &= 63;
    }

    if (len >= 48) {
      auto t3 = x3_;
      x3_ = Fold(x2_, _mm_loadu_si128(
                           reinterpret_cast<const __m128i*>(data) + 2));
      x2_ = Fold(x1_, _mm_loadu_si128(
                           reinterpret_cast<const __m128i*>(data) + 1));
      x1_ = Fold(x0_, _mm_loadu_si128(
                           reinterpret_cast<const __m128i*>(data)));
      x0_ = t3;
    } else if (len >= 32) {
      auto t2 = x2_;
      auto t3 = x3_;
      x3_ = Fold(x1_, _mm_loadu_si128(
                           reinterpret_cast<const __m128i*>(data) + 1));
      x2_ = Fold(x0_, _mm_loadu_si128(
                           reinterpret_cast<const __m128i*>(data)));
      x1_ = t3;
      x0_ = t2;
    } else if (len >= 16) {
      auto t3 = x3_;
      x3_ = Fold(x0_, _mm_loadu_si128(
                           reinterpret_cast<const __m128i*>(data)));
      x0_ = x1_;
      x1_ = x2_;
      x2_ = t3;
    }
    data += len & 48;
    len &= 15;

    if (len > 0) {
      auto xmm_shl =
          _mm_loadu_si128(reinterpret_cast<const __m128i*>(kShiftTable + len));
      auto xmm_shr = _mm_xor_si128(xmm_shl, _mm_set1_epi8(-128));

      auto t0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data));
      auto t1 = _mm_shuffle_epi8(x0_, xmm_shl);

      x0_ = _mm_or_si128(_mm_shuffle_epi8(x0_, xmm_shr),
                         _mm_shuffle_epi8(x1_, xmm_shl));
      x1_ = _mm_or_si128(_mm_shuffle_epi8(x1_, xmm_shr),
                         _mm_shuffle_epi8(x2_, xmm_shl));
      x2_ = _mm_or_si128(_mm_shuffle_epi8(x2_, xmm_shr),
                         _mm_shuffle_epi8(x3_, xmm_shl));
      x3_ = _mm_or_si128(_mm_shuffle_epi8(x3_, xmm_shr),
                         _mm_shuffle_epi8(t0, xmm_shl));

      x3_ = Fold(t1, x3_);
    }

    const auto k3k4 = _mm_set_epi32(1, 0x751997d0, 0, 0xccaa009e);
    const auto k5k4 = _mm_set_epi32(1, 0x63cd6124, 0, 0xccaa009e);
    const auto poly = _mm_set_epi32(1, 0xdb710640, 0, 0xf7011641);

    x0_ = Xor3(x1_, _mm_clmulepi64_si128(x0_, k3k4, 0x10),
               _mm_clmulepi64_si128(x0_, k3k4, 0x01));
    x0_ = Xor3(x2_, _mm_clmulepi64_si128(x0_, k3k4, 0x10),
               _mm_clmulepi64_si128(x0_, k3k4, 0x01));
    x0_ = Xor3(x3_, _mm_clmulepi64_si128(x0_, k3k4, 0x10),
               _mm_clmulepi64_si128(x0_, k3k4, 0x01));

    x1_ = _mm_xor_si128(_mm_clmulepi64_si128(x0_, k5k4, 0),
                        _mm_srli_si128(x0_, 8));

    x0_ = _mm_slli_si128(x1_, 4);
    x0_ = _mm_clmulepi64_si128(x0_, k5k4, 0x10);
#ifdef __AVX512VL__
    x0_ = _mm_ternarylogic_epi32(x0_, x1_, _mm_set_epi32(0, -1, -1, 0), 0x28);
#else
    x1_ = _mm_and_si128(x1_, _mm_set_epi32(0, -1, -1, 0));
    x0_ = _mm_xor_si128(x0_, x1_);
#endif

    x1_ = _mm_clmulepi64_si128(x0_, poly, 0);
    x1_ = _mm_clmulepi64_si128(x1_, poly, 0x10);
#ifdef __AVX512VL__
    x1_ = _mm_ternarylogic_epi32(x1_, x0_, x0_, 0xC3);
#else
    x0_ = _mm_xor_si128(x0_, _mm_set_epi32(0, -1, -1, 0));
    x1_ = _mm_xor_si128(x1_, x0_);
#endif
    return _mm_extract_epi32(x1_, 2);
  }

 private:
  alignas(32) inline static constexpr uint8_t kShiftTable[] = {
      0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
      0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
      0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};

  static inline __m128i Xor3(__m128i a, __m128i b, __m128i c) {
#ifdef __AVX512VL__
    return _mm_ternarylogic_epi32(a, b, c, 0x96);
#else
    return _mm_xor_si128(_mm_xor_si128(a, b), c);
#endif
  }

  static inline __m128i Fold(__m128i src, __m128i data) {
    const auto k1k2 = _mm_set_epi32(1, 0x54442bd4, 1, 0xc6e41596);
    return Xor3(_mm_clmulepi64_si128(src, k1k2, 0x01),
                _mm_clmulepi64_si128(src, k1k2, 0x10), data);
  }

  __m128i x0_;
  __m128i x1_;
  __m128i x2_;
  __m128i x3_;
};

}  // namespace fpnge_internal

#endif  // FPNGE_INTERNAL_CRC_X86_H_
