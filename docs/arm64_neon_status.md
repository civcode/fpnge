# ARM64 / NEON development status

## Baselines and branch policy

- Upstream source baseline: `veluca93/fpnge@097ebccf3d3db443d9a8f238b1deed010a82f961`
- Fork source baseline: `civcode/fpnge@791a79d385d773782741403486c0cd26507291c8`
- Development branch: `arm64-neon`
- Public API changes in M0: none
- Encoder source changes in M0: none

The development branch is kept separate from `main` so architecture work can be
reviewed and rebased without mixing it with unrelated fork changes.

## Milestone M0 status

Implemented in the repository:

- named compiler profiles for SSE4.1, AVX2, generic AArch64/NEON, and legacy
  native builds;
- a deterministic standalone FPNGE codec benchmark;
- compiler, CPU, kernel, governor, frequency, temperature, and throttle metadata
  capture;
- a reproducible benchmark protocol;
- an upstream design-issue draft.

Requires native hardware or the external RenderModule/FPNG environment:

- x86 SSE4.1 baseline numbers;
- x86 AVX2 baseline numbers;
- Raspberry Pi 4 FPNG fallback numbers;
- Raspberry Pi 5 FPNG fallback numbers;
- RenderModule end-to-end baseline breakdown.

Those measurements must be recorded from real target hardware. Emulation or
invented values are not acceptable substitutes.

## Milestone M1 status

Implemented on `arm64-neon`:

- direct `FPNGEEncode` API round-trip tests using pinned lodepng as an
  independent decoder;
- 8-bit and 16-bit coverage for 1, 2, 3, and 4 channels;
- RGB/BGR ordering and predictors 0 through 6;
- compression levels 1 through 5 through `FPNGEFillOptions`;
- deterministic solid, gradient, checkerboard, sparse-row, alpha, UI-like, and
  noise image families;
- widths around 16/32-byte SIMD boundaries up through 257 bytes/pixels;
- input pointer offsets 0..31 and stride padding 0..31;
- PNG chunk CRC verification plus cICP/additional-chunk semantics;
- guard regions around `FPNGEOutputAllocSize()` to detect output overwrite;
- a direct portable CRC32 differential test over lengths 0..1024 and offsets
  0..31, plus the standard "123456789" CRC vector;
- ASan/UBSan CI for the API harness;
- a focused BGR16 scalar-tail regression test.

The suspected 16-bit, three-channel BGR scalar-tail defect was confirmed and
fixed separately in commit `5e45e4751cf79a32b948e6ffb5a4ffafeae56662`.

The deterministic API suite currently contains 901 encode/decode cases, and the
portable CRC suite contains 32,800 length/alignment cases.

## Milestone M2 status

Implemented on `arm64-neon`:

- explicit `FPNGE_ARCH_X86_64` and `FPNGE_ARCH_AARCH64` detection;
- removal of the broad `__LP64` x86 predicate;
- portable and PCLMUL CRC backends split into internal headers;
- SSE4.1/AVX2 vector configuration isolated in `internal/simd_x86.h`;
- predictor/zero-run, Huffman cost/bit-pack, Adler, symbol-count, and row
  conversion mechanics isolated in `internal/kernels_x86.h`;
- shared `fpnge.cc` no longer contains direct `MIVEC` or x86 intrinsic calls;
- explicit SSE4.1 and AVX2 API CI jobs;
- byte-output parity script against the pre-M2 baseline commit.

See `docs/m2_architecture_seam.md` for the backend contract.

Native x86 performance comparison remains a hardware measurement task using the
M0 benchmark protocol; CI timing is not used as a performance gate.

## Next milestone

M3 is the short-lived AArch64 feasibility prototype. It should use the new
codec-level seams, keep the portable CRC backend, and gather correctness and
profile data on real ARM64 hardware before the production NEON implementation.
