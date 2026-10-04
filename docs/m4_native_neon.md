# Milestone M4: native AArch64 NEON backend

M4 replaces the disposable SSE2NEON feasibility route with a production
AArch64 kernel backend written directly against Arm Advanced SIMD/NEON.

## Backend selection

The shared encoder continues to call the codec-level kernel seam:

- `kernels::CopyRow`
- `kernels::CollectSymbolCounts`
- `kernels::EncodeOneRow`
- `kernels::kSimdWidth`

On x86-64 these resolve to `internal/kernels_x86.h`. On AArch64 they now
resolve to `internal/kernels_neon.h`. The public API and PNG orchestration in
`fpnge.cc` are unchanged.

The generic ARM profile remains `-march=armv8-a`. CRC32 and PMULL are not
required and the portable CRC backend remains the default on AArch64.

## Native kernel implementation

The M4 backend follows the roadmap and the Pi 5 M3 profile ordering.

### Predictor and zero-run

- 16-byte `uint8x16_t` row processing.
- Sub and Up predictors use native byte subtraction.
- Average uses `vhaddq_u8`, which directly implements the required floor
  average.
- Paeth uses native min/max, saturating subtract, compare, and bit select while
  preserving PNG tie order.
- Full-vector zero runs use AArch64 horizontal max; tails use exact prefix
  checks.

### Lookup and predictor cost

- First/last 16 literal tables use `vqtbl1q_u8`.
- Signed residual range classification is performed directly as signed NEON
  comparisons.
- Approximate and best-predictor reductions use deterministic widening sums.

### Adler-32

Each 16-byte chunk computes both the byte sum and weighted byte sum with native
widening multiply/reduction operations. The existing 5500-byte modulo flush
policy is retained.

### Huffman packing

Literal classification and lookup are vectorized with NEON. Codes are then
packed from their required DEFLATE bit representation in bounded groups rather
than reproducing the SSE variable-shift/float-emulation sequence. The grouping
helper is architecture-independent and has a randomized scalar-reference test
covering 16,705 symbol sequences, including high garbage bits above each
declared code length. This keeps the bitstream semantics explicit and leaves
headroom for `BitWriter`'s partial-byte buffer.

### BGR/RGB conversion

- 8-bit 3/4-channel conversion uses interleaved NEON loads/stores.
- 16-bit conversion operates on bytes so odd input offsets remain valid.
- 4-channel 16-bit data uses a native table permutation.
- 3-channel 16-bit data uses three byte vectors and native table lookups.
- Scalar tails remain the reference for incomplete vectors.

## M3 profile input

The clean cooled Pi 5 M3 capture at commit
`c74eb8045528e6b7f9b8690e20641ea4de85e3a4` established roughly 54.4 MP/s
for the UI workload and 45.3 MP/s for noise with the translated backend.
`SelectPredictor` and the row-encode path dominated the profile, so those
paths are the first native implementation targets. Portable CRC was material
on noise but remains deferred to M5 as planned.

These M3 numbers are comparison baselines, not claims about M4 speed.

## Validation

Run on native AArch64:

```bash
./scripts/check_aarch64_native_backend.sh
./tests/run_api_tests.sh --profile aarch64-neon
./tests/run_api_tests.sh --profile aarch64-neon --sanitize

./build.sh --profile aarch64-neon
./scripts/check_aarch64_baseline.sh ./build/fpnge
```

Capture M4 performance and profiles with:

```bash
FPNGE_M4_WARMUP=10 \
FPNGE_M4_ITERATIONS=60 \
./scripts/m4_pi_capture.sh
```

Native Pi 5 validation at
`9ed9cbecb1d4058a30cae1441d1ab2016dc9c97d` passes the 903-case API suite,
the 16,705-sequence semantic bit-pack test, the 32,800-case portable CRC test,
ASan/UBSan, and the generic ARMv8-A ISA audit.

The first cooled M4 performance capture remains below the exit gate:

| Workload | M3 translated backend | M4 native NEON | Change |
|---|---:|---:|---:|
| UI | ~54.44 MP/s | ~55.80 MP/s | +2.5% |
| Noise | ~45.31 MP/s | ~29.40 MP/s | -35.1% |

The capture remained at `throttled=0x0` and 2.4 GHz, so the noise regression
is not explained by thermal throttling. Output sizes are unchanged. The M4
`perf` reports must be reviewed before choosing the optimization; the most
likely suspect from code shape is the literal/Huffman packing path, but that is
not yet a measured attribution.

The M4 exit gate therefore remains open on performance only.

## Pi 5 M4 performance regression attribution

The first native M4 capture showed the noise workload falling to about
29.4 MP/s while UI improved to about 55.8 MP/s. The accompanying perf profile
attributes the regression directly to the literal encode callback:
`WriteLiteralChunk` accounts for roughly 60% of noise cycles, and the enclosing
literal callback accounts for roughly 67%.

Relative to the M3 translated backend, the 1280x720 noise run increased from
about 8.26 billion to 13.25 billion retired instructions and from about
547 million to 1.97 billion branches. Cache-miss rate remained low. This
isolates the regression to the first semantic packer implementation rather than
CRC, predictor selection, or memory behavior.

The hot full-vector path now constructs code lengths and bits with NEON table
lookups and packs four symbols at a time with an unrolled branchless helper.
The generic semantic sequence packer remains as the randomized reference and
is used only for the final partial vector.

## Optimized Pi 5 validation

After replacing the first semantic literal packer with a four-code unrolled
full-vector path, the native backend recovered the noise regression and moved
ahead of the M3 translated baseline on both workloads.

| Workload | M3 translated backend | Optimized M4 native NEON | Change |
|---|---:|---:|---:|
| UI | ~54.44 MP/s | ~60.51 MP/s | +11.1% |
| Noise | ~45.31 MP/s | ~51.61 MP/s | +13.9% |

The optimized capture remained at `throttled=0x0` and 2.4 GHz, with
temperatures below 50 C. Encoded output sizes match the prior M3/M4 captures.

This closes the Pi 5 side of the M4 correctness/sanitizer/ISA/performance gate.
A Pi 4 / Cortex-A72 run is still required to satisfy the detailed two-device
roadmap gate exactly.
