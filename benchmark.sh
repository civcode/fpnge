#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/build_profiles.sh
source "${ROOT_DIR}/scripts/build_profiles.sh"

usage() {
  cat <<'EOF'
Usage:
  ./benchmark.sh --profile PROFILE [--output FILE] [-- benchmark args]

Profiles:
  x86-sse41
  x86-avx2
  aarch64-neon

Example:
  ./benchmark.sh --profile x86-sse41 --output results/sse41.txt -- \
    --width 1280 --height 720 --iterations 30 --case all
EOF
}

PROFILE=""
OUTPUT=""
BENCH_ARGS=()

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
    --output)
      OUTPUT="${2:?missing value after --output}"
      shift 2
      ;;
    --output=*)
      OUTPUT="${1#*=}"
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    --)
      shift
      BENCH_ARGS=("$@")
      break
      ;;
    *)
      echo "Unknown benchmark runner argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ -z "${PROFILE}" ]]; then
  echo "--profile is required for reproducible benchmark runs." >&2
  usage >&2
  exit 2
fi

fpnge_set_build_profile "${PROFILE}"
if ! fpnge_profile_host_supported "${PROFILE}"; then
  echo "Profile ${PROFILE} is not valid on host architecture $(uname -m)." >&2
  exit 2
fi

CXX="${CXX:-clang++}"
BUILD_DIR="${ROOT_DIR}/build"
BENCH_BIN="${BUILD_DIR}/fpnge_benchmark_${PROFILE}"
mkdir -p "${BUILD_DIR}"

COMMON_FLAGS=(-O3 -g -Wall -Wextra -std=c++17)
"${CXX}" "${COMMON_FLAGS[@]}" "${FPNGE_PROFILE_CXXFLAGS[@]}" \
  "${ROOT_DIR}/fpnge.cc" "${ROOT_DIR}/bench/fpnge_benchmark.cc" \
  -o "${BENCH_BIN}"

read_first() {
  local path="$1"
  if [[ -r "${path}" ]]; then
    head -n 1 "${path}"
  else
    printf 'unknown'
  fi
}

cpu_model() {
  if command -v lscpu >/dev/null 2>&1; then
    lscpu | awk -F: '/Model name/ {sub(/^[ \t]+/, "", $2); print $2; exit}'
  elif [[ -r /proc/cpuinfo ]]; then
    awk -F: '/model name|Model/ {sub(/^[ \t]+/, "", $2); print $2; exit}' /proc/cpuinfo
  else
    printf 'unknown'
  fi
}

compiler_line="$("${CXX}" --version | head -n 1)"
git_sha="$(git -C "${ROOT_DIR}" rev-parse HEAD 2>/dev/null || printf 'unknown')"
git_branch="$(git -C "${ROOT_DIR}" rev-parse --abbrev-ref HEAD 2>/dev/null || printf 'unknown')"
governor="$(read_first /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)"
frequency_khz="$(read_first /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq)"
temperature_raw="$(read_first /sys/class/thermal/thermal_zone0/temp)"
throttle="unknown"
if command -v vcgencmd >/dev/null 2>&1; then
  throttle="$(vcgencmd get_throttled 2>/dev/null || printf 'unknown')"
fi

if [[ "${temperature_raw}" =~ ^[0-9]+$ ]]; then
  temperature_c="$(awk "BEGIN { printf \"%.1f\", ${temperature_raw} / 1000.0 }")"
else
  temperature_c="unknown"
fi

emit_run() {
  printf '# fpnge_benchmark_metadata_v1\n'
  printf '# captured_at_utc=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  printf '# git_sha=%s\n' "${git_sha}"
  printf '# git_branch=%s\n' "${git_branch}"
  printf '# profile=%s\n' "${PROFILE}"
  printf '# compiler=%s\n' "${compiler_line}"
  printf '# cxxflags=%s %s\n' "${COMMON_FLAGS[*]}" "${FPNGE_PROFILE_CXXFLAGS[*]}"
  printf '# cpu_model=%s\n' "$(cpu_model)"
  printf '# machine=%s\n' "$(uname -m)"
  printf '# kernel=%s\n' "$(uname -sr)"
  printf '# governor=%s\n' "${governor}"
  printf '# frequency_khz=%s\n' "${frequency_khz}"
  printf '# temperature_c=%s\n' "${temperature_c}"
  printf '# throttle_state=%s\n' "${throttle}"
  "${BENCH_BIN}" "${BENCH_ARGS[@]}"
}

if [[ -n "${OUTPUT}" ]]; then
  mkdir -p "$(dirname "${OUTPUT}")"
  emit_run | tee "${OUTPUT}"
else
  emit_run
fi
