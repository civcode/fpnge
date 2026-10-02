# Milestone M3 — AArch64 feasibility prototype

M3 is intentionally disposable. Its purpose is to validate the algorithm on
AArch64 quickly before the project spends time replacing the translated SSE
mechanics with native NEON kernels.

## Prototype design

The branch vendors `sse2neon` at commit
`60fc9391e378b58c60899791c0e9ee9cdaf43c08` under
`third_party/sse2neon/`.

Architecture selection is now:

- x86-64: existing SSE4.1/AVX2 configuration in `internal/simd_x86.h`;
- AArch64: 128-bit SSE-compatible operations translated by
  `internal/simd_sse2neon.h`;
- CRC on AArch64: `Crc32Portable` only.

The existing x86 kernel source is namespace-parameterized so M3 can compile it
as `arm_sse2neon` without duplicating the encoder. This is a prototype
mechanism, not the M4 production architecture.

The generic AArch64 build profile remains `-march=armv8-a`. It does not opt
into CRC32 or PMULL. `scripts/check_aarch64_baseline.sh` disassembles a built
binary and rejects those optional checksum instructions if they appear.

## Correctness validation

On native AArch64:

```bash
./tests/run_api_tests.sh --profile aarch64-neon
./build.sh --profile aarch64-neon
./scripts/check_aarch64_baseline.sh ./build/fpnge
```

The API command runs the complete M1 round-trip matrix and the portable CRC
differential suite.

The manual GitHub Actions workflow also contains an
`api-arm64-feasibility` job on the native `ubuntu-24.04-arm` runner. That job
runs the API suite, builds the generic ARMv8-A CLI, checks the baseline ISA, and
performs a small codec benchmark smoke test.

## Pi 4 / Pi 5 capture

Run the following separately on a cooled Pi 4 and Pi 5:

```bash
./scripts/m3_pi_capture.sh
```

The script captures deterministic UI and noise workloads at:

- 640x480;
- 1280x720;
- 1920x1080.

It records benchmark metadata and, when `perf` is available, hardware counters
and call-graph profiles for 1280x720 UI and noise workloads. Results are written
under `results/m3-<host>-<timestamp>/` by default.

Use environment variables to change repetitions without changing the script:

```bash
FPNGE_M3_WARMUP=10 FPNGE_M3_ITERATIONS=60 ./scripts/m3_pi_capture.sh
```

## Profile review checklist

Review native Pi profiles specifically for:

1. predictor selection / Paeth;
2. `_mm_movemask_epi8` translation used by zero-run detection;
3. Huffman table lookups and range classification;
4. `WriteBitsShort` and `WriteBitsLong`, especially SSE variable-shift
   emulation;
5. Adler-32 accumulation and horizontal reductions;
6. three-channel BGR/RGB shuffles;
7. portable CRC32.

These are hypotheses from source inspection, not measured bottlenecks. M4 work
should be prioritized from Pi profiles rather than from this list alone.

## M3 decision gate

Repository implementation is complete when the translated backend builds and
passes the M1 correctness suite on native ARM64. The project decision gate still
requires real Pi 4 and Pi 5 data.

Proceed to M4 only after the Pi captures establish that:

- the translated implementation is functionally correct;
- the codec remains plausibly competitive on Cortex-A72/A76; and
- no x86-specific assumption forces a redesign of shared PNG/Huffman control
  flow.

Do not treat GitHub-hosted ARM timing as a Raspberry Pi performance result.
