#!/bin/bash -e
set -o pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=../scripts/build_profiles.sh
source "${ROOT_DIR}/scripts/build_profiles.sh"

PROFILE="${FPNGE_TEST_PROFILE:-x86-sse41}"
SANITIZE=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --profile)
      PROFILE="${2:?missing value after --profile}"
      shift 2
      ;;
    --profile=*)
      PROFILE="${1#*=}"
      shift
      ;;
    --sanitize)
      SANITIZE=1
      shift
      ;;
    --help|-h)
      cat <<'USAGE'
Usage: ./tests/run_api_tests.sh [--profile PROFILE] [--sanitize]

Profiles are defined in scripts/build_profiles.sh. The current production
encoder is x86-only, so aarch64-neon becomes runnable after the NEON backend
lands; the profile is defined now so the same test harness can be reused.
USAGE
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

fpnge_set_build_profile "${PROFILE}"
if ! fpnge_profile_host_supported "${PROFILE}"; then
  echo "Profile ${PROFILE} is not runnable on host architecture $(uname -m)." >&2
  exit 2
fi

CXX="${CXX:-clang++}"
BUILD_DIR="${ROOT_DIR}/build/tests/${PROFILE}"
mkdir -p "${BUILD_DIR}"

LODEPNG_COMMIT=8c6a9e30576f07bf470ad6f09458a2dcd7a6a84a
if [[ ! -f "${BUILD_DIR}/lodepng.cpp" ]]; then
  wget -q "https://raw.githubusercontent.com/lvandeve/lodepng/${LODEPNG_COMMIT}/lodepng.cpp" -O "${BUILD_DIR}/lodepng.cpp"
fi
if [[ ! -f "${BUILD_DIR}/lodepng.h" ]]; then
  wget -q "https://raw.githubusercontent.com/lvandeve/lodepng/${LODEPNG_COMMIT}/lodepng.h" -O "${BUILD_DIR}/lodepng.h"
fi

COMMON_FLAGS=(-std=c++17 -Wall -Wextra -Werror)
OPT_FLAGS=(-O2 -g)
if [[ ${SANITIZE} -eq 1 ]]; then
  OPT_FLAGS=(-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined)
fi

"${CXX}" "${OPT_FLAGS[@]}" -std=c++17 -c \
  "${BUILD_DIR}/lodepng.cpp" -o "${BUILD_DIR}/lodepng.o"

"${CXX}" "${COMMON_FLAGS[@]}" "${OPT_FLAGS[@]}" \
  "${FPNGE_PROFILE_CXXFLAGS[@]}" \
  -I"${ROOT_DIR}" -I"${BUILD_DIR}" \
  "${ROOT_DIR}/fpnge.cc" "${ROOT_DIR}/tests/api_roundtrip_test.cc" \
  "${BUILD_DIR}/lodepng.o" \
  -o "${BUILD_DIR}/api_roundtrip_test"

"${BUILD_DIR}/api_roundtrip_test"

# The portable checksum backend is selected by compiling without PCLMUL. This
# test intentionally includes fpnge.cc in its own TU so Crc32 can remain an
# implementation detail of production code.
CRC_FLAGS=(-msse4.1 -mno-pclmul)
if [[ "$(uname -m)" != "x86_64" && "$(uname -m)" != "amd64" ]]; then
  echo "Portable CRC direct test is currently built from the x86 source baseline; skipping on $(uname -m)."
else
  "${CXX}" "${COMMON_FLAGS[@]}" -O2 -g "${CRC_FLAGS[@]}" \
    -I"${ROOT_DIR}" "${ROOT_DIR}/tests/crc_portable_test.cc" \
    -o "${BUILD_DIR}/crc_portable_test"
  "${BUILD_DIR}/crc_portable_test"
fi
