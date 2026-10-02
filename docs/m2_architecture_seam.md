# M2 architecture seam

Milestone M2 is an x86-only structural refactor. It does not add AArch64
instructions or change the public API.

## Active source layout

- `internal/arch.h` — explicit x86-64 and AArch64 architecture predicates.
- `internal/simd_x86.h` — SSE4.1/AVX2 vector selection, vector width, and BMI2
  PEXT policy.
- `internal/kernels_x86.h` — x86 codec kernels:
  - predictor arithmetic and zero-run detection;
  - predictor cost selection and table lookup;
  - Huffman short/long bit packing;
  - vector Adler-32 accumulation;
  - symbol collection;
  - BGR/RGB row conversion.
- `internal/crc_portable.h` — portable slice-by-8 CRC32 backend.
- `internal/crc_x86.h` — x86 PCLMUL CRC32 backend.
- `fpnge.cc` — PNG/Huffman control flow, buffer management, chunk writing, and
  backend orchestration.

The extraction intentionally keeps the existing SSE4.1/AVX2 algorithms intact.
It is a namespace/file-boundary change, not an intrinsic translation layer.

## Backend contract

The current x86 backend supplies:

- `x86::kSimdWidth` for aligned encoder scratch allocation;
- `x86::CopyRow` for channel-order conversion;
- `x86::CollectSymbolCounts` for the sampling pass; and
- `x86::EncodeOneRow` for predictor selection, Huffman packing, zero-run
  encoding, and vector Adler accumulation.

These are codec-level seams. A future NEON backend can implement equivalent
operations with ARM-native algorithms rather than reproducing x86 instruction
sequences.

CRC is selected independently from the SIMD kernel backend. Generic AArch64 can
therefore start with `Crc32Portable`, while optional ARM CRC32/PMULL support can
be added later without changing the PNG control flow.

## Regression gates

`tests/compare_x86_refactor.sh` compares encoded PNG bytes from the pre-M2
baseline commit `5e45e4751cf79a32b948e6ffb5a4ffafeae56662` with the current
tree. It tests SSE4.1 and AVX2 when available, four encoder settings, and a small
corpus of existing PNG inputs.

The M1 API suite remains the semantic correctness gate and is run for both
explicit x86 profiles in CI. ASan/UBSan continues to run on SSE4.1.

Performance remains a native-hardware measurement gate. M0's
`benchmark.sh` should be run before and after this refactor on the same x86
host; CI timing is not treated as a performance result.
