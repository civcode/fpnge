# Milestone M6: multi-architecture CI, native soak, and packaging

M6 turns the ARM64 implementation into a release-candidate validation process.
Performance remains a native-hardware gate; cloud CI is used for correctness,
sanitizers, architecture selection, package reproducibility, and short soak
smoke coverage.

## CI matrix

The manual `.github/workflows/test.yml` workflow covers:

- x86-64 SSE4.1 with PCLMUL CRC;
- x86-64 AVX2/BMI2 with PCLMUL CRC;
- x86-64 SSE4.1 with `FPNGE_FORCE_PORTABLE_CRC`;
- native Linux AArch64 with the generic `-march=armv8-a` NEON backend;
- ASan/UBSan on x86-64 and native AArch64;
- x86 refactor byte-parity coverage;
- short in-process codec soak smoke tests;
- deterministic source-package reproduction and package build/test.

The workflow remains `workflow_dispatch` only. Cloud timing is not used as a
performance gate.

## Forced portable CRC

The build profile `x86-sse41-portable-crc` defines
`FPNGE_FORCE_PORTABLE_CRC`. This gives the portable checksum path direct
cross-architecture API coverage even on x86 hosts where PCLMUL is available.
AArch64 continues to use portable CRC by default because conditional M5 was not
entered.

## Long-lived soak harness

`bench/fpnge_soak.cc` repeatedly encodes one deterministic image in a single
process. It records:

- median, p95, and p99 encode latency;
- first-quartile and last-quartile median latency plus drift percentage;
- encoded byte size and deterministic output hash;
- starting, peak, and ending RSS;
- RSS growth;
- maximum thermal-zone temperature;
- minimum and maximum sampled CPU frequency.

The harness fails if:

- output size changes;
- a sampled output stops matching the deterministic PNG/hash;
- final RSS growth exceeds the configured limit;
- last-quartile median latency exceeds the configured drift threshold.

The default limits are 8 MiB RSS growth and +25% latency drift. They can be
adjusted explicitly for a platform, but any adjustment must be recorded with
the capture.

Run directly with:

```bash
./soak.sh --profile aarch64-neon -- \
  --width 1280 --height 720 --case ui \
  --iterations 3000 --warmup 50 --sample-every 100
```

## Raspberry Pi capture

Run the same M6 capture on both required native targets:

- Raspberry Pi 4 / Cortex-A72;
- Raspberry Pi 5 / Cortex-A76.

```bash
FPNGE_M6_ITERATIONS=3000 \
FPNGE_M6_WARMUP=50 \
./scripts/m6_pi_capture.sh
```

The capture records host/commit information, backend selection, the full API
correctness suite, the generic ARMv8-A ISA check, package reproducibility,
UI/noise soak results, and Raspberry Pi throttle/temperature/frequency
snapshots before and after each soak.

A successful target capture must show:

- zero API failures;
- no unsupported CRC32/PMULL instructions in the generic binary;
- deterministic output throughout the soak;
- RSS and latency drift within the declared thresholds;
- no current or historical throttling introduced by the run;
- stable encoded byte counts.

## Reproducible source package

Create an immutable source package from a commit:

```bash
./scripts/package_source.sh \
  --commit HEAD \
  --output dist/fpnge-source.tar.gz
```

The script resolves the revision to a full commit SHA, uses `git archive`
with a SHA-specific prefix, compresses with `gzip -n`, and emits a SHA-256
sidecar. Repeating the command for the same commit must produce identical
archive bytes.

Verify that property with:

```bash
./scripts/check_reproducible_package.sh
```

The source archive intentionally represents tracked repository source at the
selected commit. Generated build/results directories are not part of it.

## Reproducible build commands

From a checkout or extracted source package:

```bash
# x86-64 baseline
./build.sh --profile x86-sse41

# x86-64 AVX2/BMI2
./build.sh --profile x86-avx2

# x86-64 with the portable CRC reference path forced
./build.sh --profile x86-sse41-portable-crc

# generic AArch64 / NEON, without optional CRC32 or PMULL
./build.sh --profile aarch64-neon
```

For correctness validation use the matching profile with
`./tests/run_api_tests.sh --profile PROFILE`.

## M6 exit gate

Repository tooling is complete when the CI matrix, soak harness, package
scripts, and build documentation have landed. The milestone itself closes only
after the candidate release commit has:

- a green manual CI run;
- a clean Pi 4 M6 capture;
- a clean Pi 5 M6 capture;
- stable soak results on both native targets;
- reproducible package evidence.

Do not substitute GitHub-hosted ARM timing for Pi performance/thermal evidence.
