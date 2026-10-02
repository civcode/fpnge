#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
machine="$(uname -m)"
if [[ "${machine}" != "aarch64" && "${machine}" != "arm64" ]]; then
  echo "M3 Pi capture must run natively on AArch64; got ${machine}." >&2
  exit 2
fi

out_dir="${1:-${ROOT_DIR}/results/m3-$(hostname)-$(date -u +%Y%m%dT%H%M%SZ)}"
iterations="${FPNGE_M3_ITERATIONS:-30}"
warmup="${FPNGE_M3_WARMUP:-5}"
mkdir -p "${out_dir}"

{
  echo "captured_at_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "hostname=$(hostname)"
  echo "machine=${machine}"
  echo "kernel=$(uname -sr)"
  echo "git_sha=$(git -C "${ROOT_DIR}" rev-parse HEAD)"
  if command -v lscpu >/dev/null 2>&1; then
    lscpu
  fi
  if command -v vcgencmd >/dev/null 2>&1; then
    vcgencmd get_throttled || true
    vcgencmd measure_temp || true
  fi
} > "${out_dir}/host.txt"

for resolution in 640x480 1280x720 1920x1080; do
  width="${resolution%x*}"
  height="${resolution#*x}"
  for case_name in ui noise; do
    "${ROOT_DIR}/benchmark.sh" --profile aarch64-neon \
      --output "${out_dir}/${resolution}-${case_name}.txt" -- \
      --width "${width}" --height "${height}" \
      --iterations "${iterations}" --warmup "${warmup}" --case "${case_name}"
  done
done

# benchmark.sh leaves the compiled codec-only benchmark here.
bench_bin="${ROOT_DIR}/build/fpnge_benchmark_aarch64-neon"
"${ROOT_DIR}/scripts/check_aarch64_baseline.sh" "${bench_bin}" | tee "${out_dir}/isa-check.txt"

if command -v perf >/dev/null 2>&1; then
  for case_name in ui noise; do
    if ! perf stat \
        -e cycles,instructions,branches,branch-misses,cache-references,cache-misses \
        -o "${out_dir}/perf-stat-1280x720-${case_name}.txt" -- \
        "${bench_bin}" --width 1280 --height 720 --iterations "${iterations}" \
        --warmup "${warmup}" --case "${case_name}" >/dev/null 2>&1; then
      echo "perf stat unavailable for ${case_name}; continuing without counters." \
        > "${out_dir}/perf-stat-1280x720-${case_name}.txt"
    fi

    if perf record -q --call-graph dwarf \
        -o "${out_dir}/perf-1280x720-${case_name}.data" -- \
        "${bench_bin}" --width 1280 --height 720 --iterations "${iterations}" \
        --warmup "${warmup}" --case "${case_name}" >/dev/null 2>&1; then
      perf report --stdio --no-children \
        -i "${out_dir}/perf-1280x720-${case_name}.data" \
        > "${out_dir}/perf-report-1280x720-${case_name}.txt" || true
    fi
  done
else
  echo "perf not installed; skipping profile capture." | tee "${out_dir}/perf-not-run.txt"
fi

echo "M3 capture written to ${out_dir}"
