#!/usr/bin/env bash

# Shared compiler profiles for reproducible FPNGE builds and benchmarks.
# Source this file and call fpnge_set_build_profile <name>.

FPNGE_PROFILE_CXXFLAGS=()

fpnge_set_build_profile() {
  local profile="${1:?build profile is required}"

  case "${profile}" in
    native)
      FPNGE_PROFILE_CXXFLAGS=(-march=native)
      ;;
    x86-sse41)
      FPNGE_PROFILE_CXXFLAGS=(-msse4.1 -mpclmul)
      ;;
    x86-avx2)
      FPNGE_PROFILE_CXXFLAGS=(-mavx2 -mbmi2 -mpclmul)
      ;;
    aarch64-neon)
      # Advanced SIMD/NEON is part of the AArch64 baseline.
      FPNGE_PROFILE_CXXFLAGS=(-march=armv8-a)
      ;;
    *)
      echo "Unknown FPNGE build profile: ${profile}" >&2
      echo "Expected one of: native, x86-sse41, x86-avx2, aarch64-neon" >&2
      return 2
      ;;
  esac
}

fpnge_profile_host_supported() {
  local profile="${1:?build profile is required}"
  local machine
  machine="$(uname -m)"

  case "${profile}" in
    native)
      return 0
      ;;
    x86-sse41|x86-avx2)
      [[ "${machine}" == "x86_64" || "${machine}" == "amd64" ]]
      ;;
    aarch64-neon)
      [[ "${machine}" == "aarch64" || "${machine}" == "arm64" ]]
      ;;
    *)
      return 2
      ;;
  esac
}
