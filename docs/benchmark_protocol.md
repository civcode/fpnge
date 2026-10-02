# FPNGE benchmark protocol

This document defines the reproducible codec-only benchmark procedure used by the
ARM64/NEON work. It is intentionally separate from RenderModule readback, row
flipping, packet construction, and transport.

## Baseline identities

- Canonical upstream source baseline: `veluca93/fpnge@097ebccf3d3db443d9a8f238b1deed010a82f961`
- Fork source baseline before ARM64 implementation work: `civcode/fpnge@791a79d385d773782741403486c0cd26507291c8`
- Documentation organization commit: `civcode/fpnge@212d5bdf3efb58959a709eefe74fda3c0811c22d`
- Development branch: `arm64-neon`

The `arm64-neon` branch is based on `main`. At the start of M0, `main` differed
from the upstream source baseline only by project documentation, so the encoder
source remained upstream-equivalent.

## Named build profiles

Shared profile definitions live in `scripts/build_profiles.sh`.

| Profile | Intended host | Compiler target flags |
|---|---|---|
| `x86-sse41` | x86-64 | `-msse4.1 -mpclmul` |
| `x86-avx2` | x86-64 | `-mavx2 -mbmi2 -mpclmul` |
| `aarch64-neon` | AArch64 | `-march=armv8-a` |
| `native` | developer convenience only | `-march=native` |

`native` is retained for compatibility with the historical build script, but it
must not be used for published benchmark comparisons.

The generic AArch64 profile deliberately does **not** enable CRC32 or PMULL.
Those are optional extensions and will be evaluated separately after the NEON
encoder exists and is profiled.

## Standalone FPNGE benchmark

The benchmark runner compiles `fpnge.cc` together with
`bench/fpnge_benchmark.cc`. It does not use the CLI decoder dependency and it
does not include file I/O in timed regions.

Example:

```bash
./benchmark.sh --profile x86-sse41 --output results/x86-sse41-1280x720.txt -- \
  --width 1280 --height 720 --iterations 30 --warmup 5 --case all
```

Repeat with `x86-avx2`. Once the AArch64 implementation exists, use the same
runner with `aarch64-neon`.

The synthetic benchmark cases are deterministic:

- `solid`
- `gradient`
- `ui`
- `noise`

Each run emits metadata followed by CSV results. Metadata includes:

- commit SHA and branch;
- compiler/version;
- complete benchmark compiler flags;
- CPU model and machine architecture;
- kernel;
- CPU governor;
- current CPU frequency where exposed by Linux;
- temperature where exposed through thermal zone 0;
- Raspberry Pi throttle state when `vcgencmd` is available.

The result columns are:

```text
case,width,height,iterations,median_ms,p95_ms,p99_ms,mp_s,encoded_bytes
```

## Required sizes and repetitions

For baseline capture, run at minimum:

```text
640x480
1280x720
1920x1080
```

Use at least 5 warm-up encodes and 30 measured iterations for published data.
For noisy workloads or thermally constrained hardware, increase repetitions
rather than selecting a best run.

## Raspberry Pi controls

For Pi 4 and Pi 5 measurements:

- use active cooling;
- use a stable power supply;
- record the governor and current frequency;
- avoid concurrent workloads;
- allow the CPU to reach a stable thermal state before comparing variants;
- record temperature and `vcgencmd get_throttled`;
- use the same compiler version and flags for A/B comparisons.

If available, pin the benchmark to one core using `taskset`, but record that in
the result notes because CPU affinity is not applied automatically by the runner.

## Baseline matrix

Numbers must be captured on the actual target machines; do not substitute
emulated or guessed results.

| Baseline | Required system | Status | Capture command / source |
|---|---|---|---|
| FPNGE SSE4.1 | native x86-64 | pending hardware run | `./benchmark.sh --profile x86-sse41 ...` |
| FPNGE AVX2 | native x86-64 | pending hardware run | `./benchmark.sh --profile x86-avx2 ...` |
| FPNG fallback | Raspberry Pi 4 / Cortex-A72 | pending RenderModule/FPNG run | use the same raw-frame corpus and resolutions |
| FPNG fallback | Raspberry Pi 5 / Cortex-A76 | pending RenderModule/FPNG run | use the same raw-frame corpus and resolutions |
| RenderModule end-to-end | Pi 4 and/or Pi 5 | pending integration run | measure readback, flip, encode, packet separately |

Raw captures should be committed under `results/` only when the environment is
stable and the data is intended to serve as a project baseline. Otherwise attach
them to the upstream design issue or PR and record the immutable commit SHA.

## Build script usage

The historical CLI build now also accepts an explicit profile:

```bash
./build.sh --profile x86-sse41
./build.sh --profile x86-avx2
```

`CXX` can be overridden in the environment:

```bash
CXX=clang++-18 ./build.sh --profile x86-avx2
```

Additional compiler flags can be passed after the profile as before.

## M0 exit gate

M0 repository infrastructure is complete when another developer can:

1. check out the exact commit;
2. select a named target profile;
3. run the benchmark with deterministic inputs;
4. see the toolchain and machine metadata in the output; and
5. reproduce the same benchmark procedure on x86 and, once the backend exists,
   AArch64.

Native Pi/FPNG numbers remain a hardware capture task; they are not inferred or
fabricated in the repository.
