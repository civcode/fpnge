#!/bin/bash -e
set -o pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FPNGE="${ROOT_DIR}/fpnge.cc"
NEON="${ROOT_DIR}/internal/kernels_neon.h"

grep -q '#include "internal/kernels_neon.h"' "${FPNGE}"
if grep -Eq 'simd_sse2neon|arm_sse2neon|third_party/sse2neon' "${FPNGE}"; then
  echo "Production AArch64 selection still references SSE2NEON." >&2
  exit 1
fi
if grep -Eq '\\b_mm[0-9A-Za-z_]*|sse2neon' "${NEON}"; then
  echo "Native NEON kernel contains an SSE/SSE2NEON dependency." >&2
  exit 1
fi
grep -q '#include <arm_neon.h>' "${NEON}"
grep -q 'vqtbl1q_u8' "${NEON}"
grep -q 'vhaddq_u8' "${NEON}"
grep -q 'vld3q_u8' "${NEON}"
grep -q 'vld4q_u8' "${NEON}"

echo "AArch64 backend check passed: production path uses native NEON kernels."
