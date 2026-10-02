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

## Next milestone

M1 builds the API-level correctness safety net before any structural SIMD
refactor or native NEON implementation begins.
