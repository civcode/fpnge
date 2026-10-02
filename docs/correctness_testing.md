# FPNGE correctness test plan

Milestone M1 establishes the correctness safety net that must stay green while
the x86 implementation is refactored and the AArch64/NEON backend is added.

## Independent round-trip validation

`tests/api_roundtrip_test.cc` calls `FPNGEEncode` directly and decodes every
result with lodepng pinned to commit
`8c6a9e30576f07bf470ad6f09458a2dcd7a6a84a`.

For every case the test checks:

1. `FPNGEEncode` stays within `FPNGEOutputAllocSize()`;
2. guard bytes before and after the advertised output allocation remain intact;
3. the PNG signature and required IHDR/IDAT/IEND chunks are present;
4. every PNG chunk CRC matches an independent scalar CRC32 implementation;
5. IHDR dimensions, bit depth, and color type match the requested API mode;
6. cICP and custom additional chunks have the expected payload when enabled;
7. lodepng decodes the image without ignoring CRC or Adler failures; and
8. decoded pixel bytes exactly match the logical source image.

For 16-bit samples the generated source and expected decode use PNG's big-endian
sample representation, matching lodepng's 16-bit raw decode contract.

## API matrix

The suite covers:

- bytes per channel: 1 and 2;
- channels: 1, 2, 3, and 4;
- RGB and BGR ordering for 3/4-channel images;
- predictors 0 through 6;
- `FPNGEFillOptions` levels 1 through 5;
- cICP metadata;
- additional ancillary chunks.

## SIMD boundary, alignment, and stride coverage

Boundary widths:

`1, 2, 3, 4, 7, 8, 9, 15, 16, 17, 31, 32, 33, 47, 48, 49, 63, 64, 65, 127, 128, 129, 255, 256, 257`

For each boundary width the suite runs all input-pointer offsets 0..31. Stride
padding is generated as a permutation of 0..31 for those offsets, so every
padding value is exercised for every width.

The dedicated BGR16 regression includes width 1, which is scalar-only, and
width 9, which runs one 8-pixel SIMD group plus a one-pixel scalar tail.

## Deterministic image families

- solid colors;
- horizontal/vertical gradients;
- checkerboards;
- sparse row changes;
- alpha gradients;
- UI/text-like patterns;
- deterministic pseudo-random noise.

No test depends on nondeterministic randomness.

## Portable CRC32 differential test

`tests/crc_portable_test.cc` compiles the existing implementation without
PCLMUL so the portable slice-by-8 backend is selected. It compares that backend
against an independent bitwise CRC32 reference for:

- every length from 0 through 1024;
- every starting offset from 0 through 31;
- direct `update_final` use;
- incremental `update` plus `update_final`; and
- the standard ASCII `123456789` vector (`0xcbf43926`).

The test includes `fpnge.cc` only in its test translation unit so `Crc32`
remains an internal production implementation detail.

## Running the tests

On x86-64:

```bash
./tests/run_api_tests.sh --profile x86-sse41
./tests/run_api_tests.sh --profile x86-sse41 --sanitize
```

The same API harness is intended to run with `--profile aarch64-neon` after
the NEON backend exists. Performance is not measured by this suite.
