#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
machine="$(uname -m)"
if [[ "${machine}" != "aarch64" && "${machine}" != "arm64" ]]; then
  echo "M6 Pi capture must run natively on AArch64; got ${machine}." >&2
  exit 2
fi

out_dir="${1:-${ROOT_DIR}/results/m6-$(hostname)-$(date -u +%Y%m%dT%H%M%SZ)}"
iterations="${FPNGE_M6_ITERATIONS:-3000}"
warmup="${FPNGE_M6_WARMUP:-50}"
sample_every="${FPNGE_M6_SAMPLE_EVERY:-100}"
max_drift="${FPNGE_M6_MAX_DRIFT_PCT:-25}"
max_rss_growth="${FPNGE_M6_MAX_RSS_GROWTH_KB:-8192}"
mkdir -p "${out_dir}"

snapshot() {
  local label="$1"
  {
    printf 'snapshot=%s\n' "${label}"
    printf 'captured_at_utc=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    if command -v vcgencmd >/dev/null 2>&1; then
      vcgencmd get_throttled || true
      vcgencmd measure_temp || true
    fi
    if [[ -r /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq ]]; then
      printf 'frequency_khz=%s\n' "$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq)"
    fi
  } >> "${out_dir}/thermal.txt"
}

{
  echo "captured_at_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "hostname=$(hostname)"
  echo "machine=${machine}"
  echo "kernel=$(uname -sr)"
  echo "git_sha=$(git -C "${ROOT_DIR}" rev-parse HEAD)"
  echo "git_branch=$(git -C "${ROOT_DIR}" rev-parse --abbrev-ref HEAD)"
  if command -v lscpu >/dev/null 2>&1; then lscpu; fi
} > "${out_dir}/host.txt"

snapshot "before-validation"

"${ROOT_DIR}/scripts/check_aarch64_native_backend.sh" |
  tee "${out_dir}/backend-check.txt"
"${ROOT_DIR}/tests/run_api_tests.sh" --profile aarch64-neon |
  tee "${out_dir}/correctness.txt"
"${ROOT_DIR}/build.sh" --profile aarch64-neon
"${ROOT_DIR}/scripts/check_aarch64_baseline.sh" "${ROOT_DIR}/build/fpnge" |
  tee "${out_dir}/isa-check.txt"
"${ROOT_DIR}/scripts/check_reproducible_package.sh" |
  tee "${out_dir}/package-check.txt"

snapshot "before-soak"

for case_name in ui noise; do
  "${ROOT_DIR}/soak.sh" --profile aarch64-neon     --output "${out_dir}/soak-1280x720-${case_name}.txt" --     --width 1280 --height 720 --case "${case_name}"     --iterations "${iterations}" --warmup "${warmup}"     --sample-every "${sample_every}"     --max-drift-pct "${max_drift}"     --max-rss-growth-kb "${max_rss_growth}"
  snapshot "after-${case_name}-soak"
done

echo "M6 capture written to ${out_dir}"
