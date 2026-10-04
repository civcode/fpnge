#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/build_profiles.sh
source "${ROOT_DIR}/scripts/build_profiles.sh"

PROFILE=""
OUTPUT=""
SOAK_ARGS=()

usage() {
  cat <<'EOF'
Usage: ./soak.sh --profile PROFILE [--output FILE] [-- soak args]

Example:
  ./soak.sh --profile aarch64-neon --output results/soak-ui.txt -- \
    --width 1280 --height 720 --case ui --iterations 1000
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --profile) PROFILE="${2:?missing value after --profile}"; shift 2 ;;
    --profile=*) PROFILE="${1#*=}"; shift ;;
    --output) OUTPUT="${2:?missing value after --output}"; shift 2 ;;
    --output=*) OUTPUT="${1#*=}"; shift ;;
    --help|-h) usage; exit 0 ;;
    --) shift; SOAK_ARGS=("$@"); break ;;
    *) echo "Unknown soak runner argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ -z "${PROFILE}" ]]; then
  echo "--profile is required." >&2
  exit 2
fi

fpnge_set_build_profile "${PROFILE}"
if ! fpnge_profile_host_supported "${PROFILE}"; then
  echo "Profile ${PROFILE} is not valid on host architecture $(uname -m)." >&2
  exit 2
fi

CXX="${CXX:-clang++}"
BUILD_DIR="${ROOT_DIR}/build"
SOAK_BIN="${BUILD_DIR}/fpnge_soak_${PROFILE}"
mkdir -p "${BUILD_DIR}"
COMMON_FLAGS=(-O3 -g -Wall -Wextra -Werror -std=c++17)
"${CXX}" "${COMMON_FLAGS[@]}" "${FPNGE_PROFILE_CXXFLAGS[@]}" \
  "${ROOT_DIR}/fpnge.cc" "${ROOT_DIR}/bench/fpnge_soak.cc" \
  -o "${SOAK_BIN}"

emit_metadata() {
  printf '# fpnge_soak_metadata_v1\n'
  printf '# captured_at_utc=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  printf '# git_sha=%s\n' "$(git -C "${ROOT_DIR}" rev-parse HEAD 2>/dev/null || printf unknown)"
  printf '# git_branch=%s\n' "$(git -C "${ROOT_DIR}" rev-parse --abbrev-ref HEAD 2>/dev/null || printf unknown)"
  printf '# profile=%s\n' "${PROFILE}"
  printf '# compiler=%s\n' "$("${CXX}" --version | head -n1)"
  printf '# cxxflags=%s %s\n' "${COMMON_FLAGS[*]}" "${FPNGE_PROFILE_CXXFLAGS[*]}"
  printf '# machine=%s\n' "$(uname -m)"
  printf '# kernel=%s\n' "$(uname -sr)"
  "${SOAK_BIN}" "${SOAK_ARGS[@]}"
}

if [[ -n "${OUTPUT}" ]]; then
  mkdir -p "$(dirname "${OUTPUT}")"
  emit_metadata | tee "${OUTPUT}"
else
  emit_metadata
fi
