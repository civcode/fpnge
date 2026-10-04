# FPNGE ARM64 / NEON Implementation Roadmap

**Repository:** `civcode/fpnge`  
**Reviewed file:** `docs/FPNGE_ARM64_NEON.md`  
**Fork snapshot:** `791a79d385d773782741403486c0cd26507291c8`  
**Upstream baseline:** `veluca93/fpnge@097ebccf3d3db443d9a8f238b1deed010a82f961`  
**Prepared:** 2026-10-02

## 1. Executive summary

The ARM64/NEON plan is technically sound and has the right priorities: preserve the public API, keep the PNG algorithm shared, make optional ARM checksum features non-mandatory, strengthen correctness before optimization, and keep the work upstreamable.

The source review shows that the implementation should be organized around **architecture-specific codec kernels**, not a broad intrinsic-for-intrinsic portability layer. `fpnge.cc` currently uses an x86 macro layer (`MM`, `MMSI`, `MIVEC`, `SIMD_WIDTH`) but also contains many direct SSE/AVX intrinsics and x86-specific bit-packing techniques. Trying to translate every intrinsic into a common abstraction would create a large internal SIMD library—the exact outcome the plan wants to avoid.

The recommended implementation sequence is therefore:

1. Freeze the baseline and strengthen correctness/benchmark infrastructure.
2. Fix or isolate pre-existing correctness issues before changing SIMD.
3. Refactor x86 code into a small set of stable kernel contracts while preserving output and performance.
4. Build a short-lived sse2neon feasibility prototype on real ARM64 hardware.
5. Implement native NEON kernels behind the same contracts.
6. Profile first, then add ARM CRC32 and PMULL only when data justifies them.
7. Add native ARM64 CI/soak coverage, integrate the fork in RenderModule, and package the change as a small upstream PR series.

A realistic planning range for one experienced SIMD/C++ engineer is **about 27-47 engineering days** through an upstream-ready patch set, excluding maintainer review latency and hardware procurement. The checksum optimization phase is optional and should be entered only after profiling.

## 2. What the source review changes from the original plan

### 2.1 The current vector abstraction is not a sufficient NEON boundary

At the top of `fpnge.cc`, AVX2 and SSE4.1 are selected through preprocessor macros. If neither is available, compilation terminates with `#error Requires SSE4.1 support minium`. This is the first hard ARM64 blocker.

However, the implementation is not fully expressed through those macros. Direct x86 intrinsics are embedded in:

- horizontal reductions and vector construction;
- `WriteBitsLong` and `WriteBitsShort`;
- predictor cost and lookup setup;
- three-channel BGR/RGB conversion in `CopyRow`;
- parts of the Adler-32 accumulation;
- PCLMUL CRC code.

**Roadmap implication:** do not define a huge universal wrapper for every x86 intrinsic. Define a small set of codec-level kernel contracts and let x86 and NEON implement those contracts using native idioms.

### 2.2 CRC already has a useful separation seam

`Crc32` already has two implementations:

- a PCLMUL-based x86 implementation when `__PCLMUL__` is available;
- a portable slice-by-8 implementation otherwise.

That means the first ARM64 encoder can use the portable checksum without blocking the port. ARM CRC32/PMULL can be introduced later behind the same checksum contract.

### 2.3 Build and CI are x86-host assumptions today

`build.sh` hardcodes `clang++` and uses `-march=native`. The only GitHub Actions job runs on `ubuntu-latest`, installs ImageMagick, and executes `test.sh`.

**Roadmap implication:** build selection and CI matrix work belong before or alongside the NEON implementation. AArch64 support should not depend on developers manually editing compiler flags.

### 2.4 Existing tests are useful but too narrow for a new SIMD backend

`test.sh` exercises corpus PNGs through the CLI and compares decoded images with ImageMagick. It does not systematically cover:

- every API image mode and option;
- SIMD tail widths;
- pointer offsets and padded strides;
- output allocation boundaries;
- deterministic randomized input;
- backend-to-backend differential behavior;
- checksum backends independently.

The original plan correctly prioritizes test hardening. The source review makes this a hard dependency for the x86 refactor and NEON port.

### 2.5 A likely pre-existing 16-bit BGR scalar-tail defect should be isolated first

In the scalar tail of `CopyRow` for 3-channel, 16-bit BGR input, the high byte of the red channel appears to be copied from `src[x * 6 + 3]` rather than `src[x * 6 + 5]`. The SIMD shuffle above it preserves the two-byte red sample as expected.

This should be confirmed with a focused regression test. If confirmed, fix it in a standalone correctness commit before the ARM work. That keeps a legacy bug fix from being conflated with NEON behavior and prevents differential tests from accidentally encoding the bug as the reference contract.

### 2.6 Architecture detection should be made explicit

`PLATFORM_AMD64` currently includes `__LP64`, which is not an x86-64 identifier and is also true on common AArch64 Unix builds. It does not currently force BMI2 on ARM because the condition also requires `__BMI2__`, but the architecture predicate is misleading and fragile.

Replace it with explicit x86-64 detection and add an explicit AArch64 predicate.

## 3. Target internal architecture

The public API in `fpnge.h` should remain unchanged. The implementation should be split only enough to create stable, testable architecture seams.

A recommended structure is:

```text
fpnge.cc                    # shared PNG/Huffman/control flow
fpnge.h                     # unchanged public API

internal/
  arch.h                    # x86_64 / aarch64 / feature macros
  simd_types.h              # minimal common types/constants, if needed
  kernels_x86.h             # SSE4.1 / AVX2 kernel implementations
  kernels_neon.h            # AArch64 NEON kernel implementations
  crc_portable.h
  crc_x86.h
  crc_arm.h
```

Exact filenames can change for upstream preference. The important rule is that `fpnge.cc` owns PNG algorithm decisions while backend headers own vector mechanics.

### 3.1 Recommended kernel contracts

Keep the cross-architecture surface small. The following are good seams because they match real hotspots in the current source:

**A. Row predictor kernel**
- Inputs: current/top/left/top-left row pointers, byte count, predictor.
- Outputs: predicted bytes in vector chunks or into a supplied buffer.
- Responsibilities: PNG Sub/Up/Average/Paeth byte arithmetic and tail-safe vector handling.

**B. Zero-run detection**
- Input: predicted vector and valid byte count.
- Output: whether all valid bytes are zero, or a compact zero mask.
- Reason: x86 `movemask` has no exact NEON equivalent; the algorithm only needs a higher-level result in several places.

**C. Huffman cost / symbol lookup kernel**
- Inputs: predicted bytes and lookup tables.
- Outputs: accumulated bit cost and/or nbits/bits vectors.
- Responsibilities: byte-table lookup, signed range classification, reductions.

**D. Huffman bit-pack kernel**
- Inputs: symbol bit lengths and low/high bit fragments.
- Output: scalar groups suitable for `BitWriter`.
- Reason: `WriteBitsShort`/`WriteBitsLong` contain x86-specific shift tricks, PEXT variants, and lane ordering. These functions should be backend-specific rather than forcing NEON to emulate SSE implementation details.

**E. Adler-32 vector accumulation**
- Inputs: predicted bytes and valid count.
- Outputs: partial `s1`/`s2` state.
- Reason: multiply-add/reduction idioms differ enough between SSE/AVX2 and NEON to justify a contained kernel.

**F. BGR-to-RGB row conversion**
- Inputs: row, channel count, bytes per channel.
- Output: converted row.
- Reason: 3-channel shuffles are architecture-specific and currently contain raw SSE intrinsics.

Do not expose every load, blend, unpack, shift, or shuffle as a public internal abstraction unless at least two kernels genuinely benefit from the same primitive.

## 4. Roadmap at a glance

| Milestone | Scope | Main output | Exit gate |
|---|---|---|---|
| M0 | Baseline and measurement | Reproducible x86/ARM baseline package | Exact SHAs, green baseline, benchmark protocol |
| M1 | Correctness hardening | API-level regression/property harness | Boundary/mode matrix green; independent decode |
| M2 | x86 architecture seams | Shared control flow + x86 kernels | Byte/output parity and no material x86 regression |
| M3 | ARM feasibility prototype | sse2neon or minimal translated build | Native Pi performance/feasibility decision |
| M4 | Native NEON | Production AArch64 kernel backend | Full correctness matrix and target speedup trend |
| M5 | ARM checksum (conditional) | CRC32/PMULL backend | Measured win, safe feature gating |
| M6 | CI, native soak, packaging | Multi-arch CI + sustained validation | ARM/x86 green; soak and thermal data |
| M7 | Integration/upstream | RenderModule pin + upstream PR series | Reviewable commits and integration evidence |

**Total planning range:** 27-47 engineering days for M0-M7 when M5 is needed. Without ARM checksum work, plan roughly 24-41 days. These are engineering-effort ranges, not calendar commitments.

## 5. Detailed implementation plan

## Milestone M0 — Freeze baseline and establish measurement

### Tasks

1. Record the fork and upstream baselines:
   - fork: `civcode/fpnge@791a79d385d773782741403486c0cd26507291c8`;
   - upstream: `veluca93/fpnge@097ebccf3d3db443d9a8f238b1deed010a82f961`.
2. Create `arm64-neon` from the upstream-equivalent baseline.
3. Add explicit toolchain metadata to benchmark output:
   - compiler and version;
   - compile flags;
   - CPU model;
   - kernel;
   - governor/frequency;
   - temperature/throttle state where available.
4. Replace benchmark-only use of `-march=native` with named build profiles:
   - x86 SSE4.1 baseline;
   - x86 AVX2;
   - AArch64 generic NEON;
   - optional ARM feature builds later.
5. Produce baseline numbers for:
   - upstream FPNGE SSE4.1 and AVX2 on x86;
   - FPNG on Pi 4 / Pi 5;
   - current RenderModule end-to-end encode path if available.
6. Open/prepare the upstream design issue before structural refactoring.

### Deliverables

- `docs/benchmark_protocol.md` or equivalent;
- reproducible benchmark command/script;
- baseline result table committed as data or attached to the design issue;
- branch and commit policy documented.

### Exit gate

Do not refactor SIMD until the baseline can be rebuilt and rerun by another developer.

## Milestone M1 — Build a correctness safety net

### Tasks

1. Add an API-level test binary that calls `FPNGEEncode` directly rather than only the CLI.
2. Decode every produced PNG with an independent decoder and compare:
   - dimensions;
   - channels/bit depth semantics;
   - pixel bytes;
   - relevant metadata/chunks.
3. Cover the full API matrix:
   - bytes/channel: 1, 2;
   - channels: 1, 2, 3, 4;
   - RGB and BGR ordering where applicable;
   - predictors 0-6;
   - compression levels used by `FPNGEFillOptions`;
   - cICP and additional chunks.
4. Add deterministic generated image families:
   - solid;
   - gradients;
   - checkerboard;
   - sparse row deltas;
   - alpha patterns;
   - UI/text-like shapes;
   - random/noise.
5. Add width and byte-count cases around 16- and 32-byte boundaries.
6. Add pointer-offset and stride-padding cases for offsets/padding 0..31.
7. Guard the output allocation and verify no write beyond `FPNGEOutputAllocSize`.
8. Add ASan/UBSan jobs where supported.
9. Add direct checksum tests for the portable CRC implementation.
10. Add a targeted 16-bit, 3-channel BGR tail test and confirm/fix the suspected `CopyRow` scalar bug as a separate commit.

### Test design recommendation

Prefer golden **decoded pixels and semantics** over golden PNG byte streams. Also record byte-identical output as a regression signal for x86-refactor work, but do not make it the public portability contract.

### Exit gate

The test suite must detect:
- a one-byte channel-order error;
- a tail off-by-one error;
- a checksum mismatch;
- an output buffer overwrite;
- a predictor mismatch that changes decoded pixels.

## Milestone M2 — Refactor x86 into stable kernel seams

This is the highest-risk maintainability milestone. The goal is to change structure without adding ARM code.

### Tasks

1. Introduce explicit architecture macros:
   - `FPNGE_ARCH_X86_64`;
   - `FPNGE_ARCH_AARCH64`.
2. Remove `__LP64` from x86-64 detection.
3. Move checksum code into portable/x86 backend units or headers without changing behavior.
4. Extract the six kernel areas described in Section 3.1.
5. Keep SSE4.1 and AVX2 behavior available through the same compile-time selection as today.
6. Replace raw x86 operations in shared control flow with kernel calls.
7. Preserve current vector widths:
   - SSE4.1: 16 bytes;
   - AVX2: 32 bytes.
8. Avoid virtual dispatch and heap allocation in hot paths.
9. Verify generated code/inlining with compiler reports or disassembly on representative functions.

### Design rule for bit packing

Do **not** require NEON to reproduce the SSE float-exponent shift workaround or PEXT path. `WriteBitsShort` and `WriteBitsLong` may have separate x86 and NEON implementations as long as they produce the same bitstream semantics.

### Exit gate

- All M1 tests green.
- Regression corpus byte-identical to pre-refactor x86 output, except any separately documented correctness fix.
- SSE4.1 and AVX2 benchmarks show no statistically meaningful regression.
- Shared `fpnge.cc` no longer directly includes x86 intrinsic types outside clearly isolated backend glue.

### Suggested upstream PR

This milestone is a strong standalone PR: “Refactor architecture-specific kernels; no functional change.”

## Milestone M3 — AArch64 feasibility prototype

The goal is a fast answer, not production code.

### Tasks

1. Build a temporary ARM64 path using sse2neon or a minimal compatibility shim.
2. Keep the portable CRC path.
3. Compile with a generic AArch64 target that assumes baseline Advanced SIMD only.
4. Run correctness tests on real Pi 4 and Pi 5.
5. Benchmark at minimum:
   - 640x480;
   - 1280x720;
   - 1920x1080;
   - representative UI and noisy frames.
6. Profile:
   - predictor selection;
   - bit packing;
   - Adler-32;
   - BGR/RGB conversion if used;
   - CRC.
7. Record operations that translate poorly or expand into unexpectedly expensive sequences.

### Decision gate

Proceed to native NEON if:
- the translated implementation is functionally correct;
- the profile indicates the algorithm remains competitive on Cortex-A72/A76;
- no fundamental x86-only assumption requires redesigning the PNG algorithm.

Stop and reassess if the translated version cannot materially approach FPNG even after obvious translation inefficiencies are accounted for.

## Milestone M4 — Native NEON implementation

### Build selection

On AArch64, baseline NEON/Advanced SIMD can be treated as part of the architecture target. Do not require CRC32 or PMULL for the generic NEON build.

### Kernel-by-kernel implementation order

**1. Predictor + zero-run kernel**
- implement loads/stores and byte arithmetic;
- implement PNG Average exactly;
- port Paeth carefully;
- replace `movemask` dependence with a NEON-native all-zero or compact-mask helper;
- validate every predictor against scalar reference data.

**2. Lookup and cost kernel**
- use `vqtbl1q_u8`/table lookup patterns for byte LUTs where appropriate;
- verify signed comparisons exactly match the SSE range test;
- make reductions deterministic.

**3. Adler-32 kernel**
- use widening pairwise sums/multiply-accumulate idioms;
- keep the same modulo flush policy unless profiling proves a reason to change it;
- test against the scalar formula at every buffer length around flush thresholds.

**4. Huffman bit packing**
- implement native NEON packing based on the required bitstream result, not on SSE instruction correspondence;
- keep a scalar reference packer available in tests;
- compare bit output for randomized symbol sequences.

**5. BGR/RGB row conversion**
- implement 4-channel byte/word shuffles;
- implement 3-channel conversion using NEON table/interleaved load-store patterns as appropriate;
- retain scalar tails and test every tail length.

**6. Integration**
- select `kernels_neon.h` for AArch64;
- keep public API unchanged;
- keep portable CRC as the default ARM checksum.

### Exit gate

- Full M1 matrix green on native AArch64.
- Zero sanitizer findings on supported ARM64 sanitizer builds.
- Byte-identical x86 and ARM PNG output for the deterministic corpus where the algorithm path is intended to be identical.
- No unsupported ARM extension appears in the generic AArch64 binary.
- Representative Pi 4/Pi 5 throughput is clearly above FPNG or close enough that profiling identifies a specific, tractable remaining hotspot.

## Milestone M5 — ARM checksum acceleration (conditional)

Enter this milestone only if profiling shows checksum work is material.

### Stage 1: ARM CRC32 instructions

Implement an accelerated CRC backend using ARM CRC32 instructions when available. Keep:
- portable slice-by-8 as the trusted reference;
- compile-time or runtime feature gating;
- direct differential tests for lengths, alignments, and incremental update boundaries.

### Stage 2: PMULL

Only add PMULL if:
- CRC remains a significant percentage of encode time after Stage 1; or
- large IDAT chunks make polynomial folding measurably better on target hardware.

### Feature-dispatch choices

Preferred order:
1. compile separate feature-specific translation units and dispatch at runtime;
2. use function target attributes if toolchain support is proven across supported compilers;
3. compile-time selection for platform builds that intentionally target a fixed CPU.

For Linux runtime dispatch, use platform feature mechanisms such as `getauxval(AT_HWCAP)` rather than executing an instruction and catching failure.

### Exit gate

- Accelerated CRC exactly matches portable CRC.
- Generic AArch64 binary is safe on machines without optional extensions.
- The optimized path produces a repeatable end-to-end codec win, not just a microbenchmark win.

## Milestone M6 — Multi-architecture CI, native hardware, and soak

### CI matrix

At minimum:

- Linux x86-64 SSE4.1;
- Linux x86-64 AVX2;
- Linux AArch64 compile + functional tests;
- ASan/UBSan where practical;
- portable CRC forced build;
- optimized CRC build when CI hardware supports it.

Emulation/cross-compilation is acceptable for build and functional coverage. Performance gates must run on native hardware.

### Native validation

Required:
- Raspberry Pi 4 / Cortex-A72;
- Raspberry Pi 5 / Cortex-A76.

Preferred:
- one cloud/server AArch64 Linux host.

### Soak tests

Run repeated encoding/streaming long enough to detect:
- allocation growth;
- crashes;
- invalid PNGs;
- latency drift;
- thermal throttling;
- performance collapse after warm-up.

Record median, p95, and p99 encode latency plus encoded size.

### Exit gate

A candidate release commit must have:
- green cross-architecture correctness;
- native Pi benchmark data;
- stable soak results;
- reproducible build instructions.

## Milestone M7 — RenderModule integration and upstream submission

### RenderModule integration

1. Pin the fork to an immutable commit.
2. Enable FPNGE on supported AArch64 builds.
3. Keep FPNG available as fallback/rescue.
4. Log the selected PNG backend and architecture path.
5. Run RenderModule image correctness tests on ARM.
6. Measure pipeline components separately:
   - readback;
   - row flip;
   - PNG encode;
   - packet construction;
   - transport.

Do not optimize non-codec stages inside the FPNGE fork.

### Upstream PR series

Recommended sequence:

1. **Tests + isolated correctness fix**
   - stronger API tests;
   - BGR16 tail fix if confirmed;
   - benchmark harness.
2. **Architecture/kernel refactor**
   - x86-only behavior preserved.
3. **AArch64 NEON backend**
   - portable CRC.
4. **Optional ARM checksum acceleration**
   - only if justified by profile.
5. **CI/documentation**
   - architecture support and benchmark notes.

Each PR should include:
- supported compilers/flags;
- test matrix;
- before/after x86 performance where relevant;
- ARM hardware details;
- encoded-size comparison;
- clear explanation of optional CPU feature handling.

## 6. Work breakdown by current source area

| Current source area | Refactor action | ARM64 implementation concern |
|---|---|---|
| Architecture macros at top of `fpnge.cc` | Replace with explicit x86/AArch64 selection | Avoid broad `__LP64`; allow baseline NEON |
| `Crc32` | Split portable/x86/ARM backends | CRC32/PMULL optional; portable default first |
| `PredictVec` / `ProcessRow` | Move predictor mechanics into kernel backend | Paeth byte semantics; zero-run test without movemask |
| `TryPredictor` / `AddApproxCost` / `SelectPredictor` | Separate table lookup/reduction primitives | `vqtbl` semantics, signed comparisons, reduction order |
| `WriteBitsShort` / `WriteBitsLong` | Treat as backend-specific bit-pack kernels | Do not emulate SSE float tricks; verify exact bitstream |
| Adler code inside `EncodeOneRow` | Encapsulate vector accumulator | widening multiply-add and partial-tail semantics |
| `CopyRow` | Backend conversion kernel + scalar reference | 3-channel shuffles; verify suspected BGR16 tail issue |
| `FPNGEEncode` | Keep architecture-neutral | only width/alignment constants should come from backend |
| `build.sh` | Add target-aware flags/profiles | no unconditional `-march=native` |
| `.github/workflows/test.yml` | Add matrix/native ARM coverage | native perf separate from functional emulation |

## 7. Performance plan and merge thresholds

### Benchmarks

For each corpus/resolution report:
- encode time;
- MP/s;
- output bytes;
- median/p95/p99;
- cycles/pixel when available;
- relevant PMU counters for hotspot investigations.

### Comparison set

- FPNG fallback on ARM;
- FPNGE SSE4.1 on x86;
- FPNGE AVX2 on x86;
- FPNGE NEON + portable CRC;
- FPNGE NEON + ARM CRC, if implemented;
- FPNGE NEON + PMULL, if implemented.

### Proposed thresholds

**Correctness:** absolute gate; no exceptions.

**x86 refactor:** no statistically meaningful throughput regression on the agreed corpus. If a small regression is accepted for maintainability, document it explicitly with maintainer agreement.

**ARM:** use the planning document's 1.5x FPNG throughput at 1280x720 and 1920x1080 representative UI workloads as the target, not as a reason to weaken correctness or portability.

**Encoded size:** no meaningful regression versus the same FPNGE compression mode.

### Measurement discipline

For Raspberry Pi:
- active cooling;
- stable power;
- warm-up;
- recorded governor/frequency;
- throttling/temperature log;
- CPU affinity where useful;
- repeated runs;
- same compiler and flags for A/B comparisons.

## 8. Risk register

| Risk | Impact | Mitigation |
|---|---|---|
| Over-general SIMD abstraction | Large review surface, x86 regression | Use codec-kernel contracts, not intrinsic emulation |
| Bit-packing semantics differ | Corrupt PNG/rare failures | Scalar reference packer + randomized bitstream differential tests |
| `movemask` translation becomes expensive | NEON predictor/RLE slowdown | Express required predicates directly; benchmark alternatives |
| Optional ARM instructions leak into baseline | Illegal instruction on older/limited CPUs | Separate TUs/target attributes + runtime feature detection |
| Existing x86 bug becomes “reference behavior” | Cross-arch tests codify defect | Isolate/fix known defects before port |
| Cross-compile CI gives false performance confidence | Bad optimization decisions | Native Pi performance gates only |
| AVX2 regresses during refactor | Upstream resistance | Preserve AVX2 specialization and benchmark every extraction step |
| sse2neon prototype becomes permanent | Dependency/maintenance cost | Label prototype disposable; native NEON is merge gate |
| Thermal throttling distorts Pi results | Misleading throughput claims | Active cooling + temperature/throttle recording |
| Upstream prefers different internal layout | Rework | Open architecture issue before deep refactor |

## 9. Definition of done

The ARM64/NEON project is complete when all of the following are true:

- `FPNGEEncode`, `FPNGEFillOptions`, and `FPNGEOutputAllocSize` remain API-compatible.
- 8/16-bit and 1/2/3/4-channel coverage passes on x86 and AArch64.
- RGB/BGR, predictor, stride, alignment, metadata/chunk, and boundary cases are covered.
- Deterministic randomized/property tests report zero decoded-pixel mismatches.
- Sanitizer builds report no encoder memory or UB failures.
- Generic AArch64 uses only baseline-safe instructions.
- Optional CRC32/PMULL paths cannot execute on unsupported CPUs.
- SSE4.1 and AVX2 regression suites remain green.
- x86 performance is preserved within the agreed noise/acceptance band.
- Pi 4 and Pi 5 show a meaningful throughput advantage over FPNG on representative workloads.
- Long-running ARM tests remain stable under thermal steady state.
- The FPNGE fork contains no RenderModule-specific encoding behavior.
- The commit series is reviewable, bisectable, and suitable for upstream submission.
- RenderModule can pin the fork during review and return to upstream after merge.

## 10. First two weeks: concrete execution order

Assuming one experienced engineer, the most useful initial sequence is:

**Days 1-2**
- create `arm64-neon`;
- freeze SHAs and build profiles;
- capture x86 and Pi FPNG baselines;
- prepare upstream design issue.

**Days 3-6**
- add direct API correctness harness;
- add dimensions/strides/alignment matrix;
- add independent decoding;
- add checksum tests;
- confirm and separately fix the BGR16 scalar-tail issue if reproducible.

**Days 7-10**
- extract checksum backend;
- introduce explicit architecture detection;
- extract predictor/zero-run and row-conversion kernels;
- keep x86 output byte-identical;
- benchmark after each extraction.

At the end of this period, the project should have a safe base for the more difficult bit-packing extraction and the ARM feasibility prototype.

## 11. Recommended issue/PR backlog

1. `test: add API-level round-trip matrix and independent decoder`
2. `test: add alignment, stride, SIMD-tail, and output-boundary coverage`
3. `fix: correct 16-bit three-channel BGR scalar tail` — only if reproduced
4. `build: make architecture targets explicit`
5. `refactor: isolate CRC backends`
6. `refactor: isolate row predictor and zero-run kernels`
7. `refactor: isolate BGR/RGB conversion kernel`
8. `refactor: isolate Huffman cost and bit-pack kernels`
9. `prototype: bring up AArch64 with temporary sse2neon path`
10. `arm64: implement native NEON predictor/cost kernels`
11. `arm64: implement native NEON Huffman bit packing`
12. `arm64: implement native NEON Adler and row conversion`
13. `arm64: add optional CRC32 backend`
14. `arm64: add PMULL CRC backend` — only if profiling justifies
15. `ci: add AArch64 functional matrix and sanitizer coverage`
16. `bench: add Pi 4/Pi 5 reproducible benchmark runner`
17. `docs: document supported architectures and feature dispatch`
18. `integration: enable FPNGE ARM64 in RenderModule fork pin`
19. `upstream: submit architecture refactor and NEON PR series`

## 12. Source references

- Fork repository: https://github.com/civcode/fpnge
- Reviewed plan: https://github.com/civcode/fpnge/blob/main/docs/FPNGE_ARM64_NEON.md
- Current implementation: https://github.com/civcode/fpnge/blob/main/fpnge.cc
- Public API: https://github.com/civcode/fpnge/blob/main/fpnge.h
- Build script: https://github.com/civcode/fpnge/blob/main/build.sh
- Test script: https://github.com/civcode/fpnge/blob/main/test.sh
- CI workflow: https://github.com/civcode/fpnge/blob/main/.github/workflows/test.yml
- Upstream repository: https://github.com/veluca93/fpnge
- Upstream contribution guidance: https://github.com/civcode/fpnge/blob/main/CONTRIBUTING.md