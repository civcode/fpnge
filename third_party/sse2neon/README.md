# sse2neon dependency

This directory vendors `sse2neon.h` from
`DLTcollab/sse2neon@60fc9391e378b58c60899791c0e9ee9cdaf43c08`.

It is used only by the Milestone M3 AArch64 feasibility prototype so the
existing SSE4.1 kernel implementation can be exercised on baseline AArch64
without designing the production NEON backend prematurely.

The production Milestone M4 backend is expected to replace this compatibility
path with native NEON codec kernels. The upstream MIT license is included in
this directory.
