// Copyright 2026
#ifndef FPNGE_INTERNAL_SIMD_X86_H_
#define FPNGE_INTERNAL_SIMD_X86_H_

#if !FPNGE_ARCH_X86_64
#error "Current FPNGE SIMD backend supports x86-64 only; AArch64/NEON is not implemented yet"
#endif

#ifdef __AVX2__
#include <immintrin.h>
#define MM(f) _mm256_##f
#define MMSI(f) _mm256_##f##_si256
#define MIVEC __m256i
#define BCAST128 _mm256_broadcastsi128_si256
// workaround for compilers not supporting _mm256_zextsi128_si256
#if (defined(__clang__) && __clang_major__ >= 5 &&                             \
     (!defined(__APPLE__) || __clang_major__ >= 7)) ||                         \
    (defined(__GNUC__) && __GNUC__ >= 10) ||                                   \
    (defined(_MSC_VER) && _MSC_VER >= 1910)
#define INT2VEC(v) _mm256_zextsi128_si256(_mm_cvtsi32_si128(v))
#elif defined(__OPTIMIZE__)
// technically incorrect, but should work fine most of the time
#define INT2VEC(v) _mm256_castsi128_si256(_mm_cvtsi32_si128(v))
#else
// _mm256_insert_epi32 is unavailable on MSVC 19.0, so prefer the following
#define INT2VEC(v)                                                             \
  _mm256_inserti128_si256(_mm256_setzero_si256(), _mm_cvtsi32_si128(v), 0);
#endif
#define SIMD_WIDTH 32
#define SIMD_MASK 0xffffffffU
#elif defined(__SSE4_1__)
#include <nmmintrin.h>
#define MM(f) _mm_##f
#define MMSI(f) _mm_##f##_si128
#define MIVEC __m128i
#define BCAST128(v) (v)
#define INT2VEC _mm_cvtsi32_si128
#define SIMD_WIDTH 16
#define SIMD_MASK 0xffffU
#else
#error Requires SSE4.1 support minium
#endif


#endif  // FPNGE_INTERNAL_SIMD_X86_H_
