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

The deterministic API suite currently contains 903 encode/decode cases, and the
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

## Milestone M3 status

Repository-side feasibility implementation is complete:

- pinned `sse2neon` compatibility dependency for the disposable prototype;
- AArch64 backend selection using the existing 128-bit kernel source;
- portable CRC32 forced on AArch64;
- generic `-march=armv8-a` build profile retained;
- baseline-ISA audit that rejects CRC32/PMULL in generic ARM binaries;
- native ARM64 GitHub Actions correctness/build/benchmark smoke job;
- Pi 4/Pi 5 benchmark and `perf` capture script for UI and noise workloads at
  640x480, 1280x720, and 1920x1080.

See `docs/m3_feasibility.md`.

A clean native Pi 5 / Cortex-A76 capture is now available and was used to
prioritize M4. The original two-device M3 decision gate still needs a Pi 4 /
Cortex-A72 capture if it is to be closed exactly as written.

## Milestone M4 status

Repository-side native NEON implementation has landed:

- production AArch64 selection now uses `internal/kernels_neon.h`;
- SSE2NEON is no longer part of the production AArch64 include path;
- predictor, Paeth, zero-run, LUT/cost, Adler, Huffman packing, symbol count,
  and BGR/RGB conversion paths have native NEON implementations;
- portable CRC remains the generic ARM checksum backend;
- a static backend guard rejects accidental SSE/SSE2NEON dependencies;
- the semantic Huffman grouping helper has a 16,705-sequence randomized
  scalar-reference test;
- native ARM64 correctness and sanitizer jobs are defined in the manual test
  workflow;
- `scripts/m4_pi_capture.sh` captures correctness-adjacent ISA metadata,
  throughput, counters, and profiles for the same M3 workload matrix.

See `docs/m4_native_neon.md`.

Native Pi 5 validation at `9ed9cbecb1d4058a30cae1441d1ab2016dc9c97d`
now passes the 903-case API suite, the 16,705-sequence semantic bit-pack test,
the 32,800-case portable CRC test, the ASan/UBSan run, and the generic ARMv8-A
ISA audit.

The cooled Pi 5 M4 capture is thermally clean (`throttled=0x0`) and shows a
split result: UI throughput improves from the clean M3 baseline of about
54.44 MP/s to about 55.80 MP/s (+2.5%), while noise drops from about
45.31 MP/s to about 29.40 MP/s (-35.1%). The output sizes are unchanged.

The **M4 correctness/sanitizer/ISA gates are green, but the M4 performance exit
gate is not met**. The noise regression must be profiled and fixed before M4 is
closed. The current leading hypothesis is the native literal/Huffman packing
path because incompressible noise exercises it far more heavily; treat that as
a hypothesis until the M4 `perf` reports are reviewed.

## Next milestone

Do not begin M5 checksum acceleration until the M4 native run is profiled.
Portable CRC was material in the M3 noise profile, but M5 is conditional on
the post-M4 profile.

## M3 Pi 5 bring-up finding

Native Pi 5 testing exposed a second pre-existing scalar-tail channel-order bug
in 16-bit four-channel BGR/BGRA conversion. The high byte of the red channel
was copied from the green sample in the scalar tail. The SIMD shuffle was
already correct, so failures appeared on odd widths that reached the scalar
tail. The fix is covered by dedicated 3-channel and 4-channel BGR16 tail
regression cases.

## M4 sanitizer finding

The first native Pi 5 M4 sanitizer run exposed undefined behavior in the
portable slice-by-8 CRC backend: its 32-bit loads assumed pointer alignment
while IDAT data can begin at arbitrary byte alignment. The backend now performs
explicit little-endian byte loads, removing both the alignment UB and the
native-endian load assumption. Sanitizer builds also use
`-fno-sanitize-recover=all`, and the CRC/bit-pack unit tests are compiled with
the sanitizer flags so future findings fail the test command immediately.
