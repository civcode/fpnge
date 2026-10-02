// Copyright 2026
#ifndef FPNGE_INTERNAL_SIMD_SSE2NEON_H_
#define FPNGE_INTERNAL_SIMD_SSE2NEON_H_

#include "arch.h"

#if !FPNGE_ARCH_AARCH64
#error "The sse2neon feasibility backend is only valid on AArch64"
#endif

// Milestone M3 is deliberately a compatibility prototype. Baseline AArch64
// includes Advanced SIMD/NEON, while optional CRC32/PMULL extensions remain
// disabled by the generic -march=armv8-a build profile.
#include "../third_party/sse2neon/sse2neon.h"

#define MM(f) _mm_##f
#define MMSI(f) _mm_##f##_si128
#define MIVEC __m128i
#define BCAST128(v) (v)
#define INT2VEC _mm_cvtsi32_si128
#define SIMD_WIDTH 16
#define SIMD_MASK 0xffffU

// BMI2 PEXT is an x86-only bit-packing optimization.
#define FPNGE_USE_PEXT 0

#endif  // FPNGE_INTERNAL_SIMD_SSE2NEON_H_
