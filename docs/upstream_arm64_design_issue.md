# Draft upstream issue: AArch64 / NEON support

**Suggested title:** AArch64/NEON support with isolated architecture kernels

FPNGE currently requires x86 SIMD (SSE4.1, with optional AVX2 and x86 checksum
acceleration). I am working on an AArch64 implementation using baseline NEON /
Advanced SIMD while preserving the current public API and x86 implementations.

Before doing a large refactor, I would like feedback on the preferred internal
architecture.

## Proposed direction

Keep the PNG/Huffman control flow shared, but isolate a small number of
architecture-specific codec kernels instead of introducing a general
intrinsic-for-intrinsic portability layer.

The likely seams are:

1. row prediction and tail handling;
2. zero-run detection;
3. Huffman lookup/cost accumulation;
4. Huffman bit packing;
5. Adler-32 vector accumulation;
6. BGR/RGB row conversion;
7. checksum backends.

The current SSE/AVX code contains direct x86 idioms in bit packing, reductions,
three-channel shuffles, and checksum code, so a tiny instruction wrapper would
not fully isolate the architecture while a complete SIMD wrapper would be much
larger than this project needs.

## Intended support

- x86-64 SSE4.1: preserved
- x86-64 AVX2: preserved
- AArch64 NEON/Advanced SIMD: new
- portable CRC: valid on all targets
- ARM CRC32/PMULL: optional follow-up only if profiling shows an end-to-end win

No public API change is planned.

## Correctness and performance plan

Before the architecture refactor, the fork will add API-level round-trip tests,
SIMD-boundary dimensions, stride/alignment cases, deterministic randomized
inputs, checksum differential tests, and sanitizer coverage.

The x86-only refactor should preserve encoded output for the regression corpus
and avoid a measurable SSE4.1/AVX2 regression. Native ARM performance decisions
will be based on real AArch64 hardware, not emulation.

## Proposed PR split

1. tests/benchmark hardening and any isolated pre-existing correctness fix;
2. x86 architecture-kernel refactor with no intended functional change;
3. native AArch64 NEON backend using portable CRC;
4. optional ARM checksum acceleration if justified;
5. CI/documentation.

Would this kernel-level separation and PR split fit the direction you would
prefer for upstream FPNGE? In particular, are there internal layout or build
constraints you would like the port to follow before the refactor begins?
