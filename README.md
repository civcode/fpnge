# Fast PNG Encoder

> **Fork notice:** This repository is a fork of [veluca93/fpnge](https://github.com/veluca93/fpnge).
> The purpose of this fork is to add production-quality AArch64/ARM64 support using
> NEON/Advanced SIMD while preserving the existing x86 SSE4.1/AVX2 paths and public API.
> The work is being structured for eventual upstream contribution; see [docs/](docs/) for
> the implementation plan, roadmap, benchmark protocol, and current ARM64 development status.
This is a proof-of-concept fast PNG encoder that uses AVX2 and a special
Huffman table to encode images faster. Speed on a single core is anywhere from
180 to 800 MP/s on a Threadripper 3970x, depending on compile time settings and
content.

It supports 8 and 16 bit content, 1 to 4 channels; it can also emit
[cICP chunks](https://www.w3.org/TR/png/#cICP-chunk) for signaling that
the content should be interpreted as HDR.
