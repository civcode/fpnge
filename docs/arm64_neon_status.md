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

## Next milestone

M2 extracts explicit architecture detection and stable x86 kernel/checksum
seams without adding ARM implementation code.
