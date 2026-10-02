#!/bin/bash -e
set -o pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BASELINE="${FPNGE_X86_REFACTOR_BASELINE:-5e45e4751cf79a32b948e6ffb5a4ffafeae56662}"
CXX="${CXX:-clang++}"
TMP_DIR="$(mktemp -d)"
trap 'git -C "${ROOT_DIR}" worktree remove --force "${TMP_DIR}/baseline" >/dev/null 2>&1 || true; rm -rf "${TMP_DIR}"' EXIT

git -C "${ROOT_DIR}" worktree add --detach "${TMP_DIR}/baseline" "${BASELINE}" >/dev/null

inputs=("${ROOT_DIR}/testdata/terminal.png")
while IFS= read -r file; do
  inputs+=("${file}")
done < <(find "${ROOT_DIR}/jxl_testdata" -name '*.png' -type f | sort | head -n 5)

profiles=(x86-sse41)
if grep -qw avx2 /proc/cpuinfo 2>/dev/null; then
  profiles+=(x86-avx2)
fi

settings=("" "-1" "-2" "-3")
cases=0

for profile in "${profiles[@]}"; do
  CXX="${CXX}" "${TMP_DIR}/baseline/build.sh" --profile "${profile}" >/dev/null
  CXX="${CXX}" "${ROOT_DIR}/build.sh" --profile "${profile}" >/dev/null

  for input in "${inputs[@]}"; do
    for setting in "${settings[@]}"; do
      old_png="${TMP_DIR}/old.png"
      new_png="${TMP_DIR}/new.png"
      if [[ -n "${setting}" ]]; then
        "${TMP_DIR}/baseline/build/fpnge" "${setting}" "${input}" "${old_png}"
        "${ROOT_DIR}/build/fpnge" "${setting}" "${input}" "${new_png}"
      else
        "${TMP_DIR}/baseline/build/fpnge" "${input}" "${old_png}"
        "${ROOT_DIR}/build/fpnge" "${input}" "${new_png}"
      fi
      if ! cmp -s "${old_png}" "${new_png}"; then
        echo "Byte-output regression: profile=${profile} setting=${setting:-default} input=${input}" >&2
        cmp -l "${old_png}" "${new_png}" | head -n 20 >&2 || true
        exit 1
      fi
      cases=$((cases + 1))
    done
  done
done

echo "x86 refactor parity: ${cases} byte-identical encode cases passed against ${BASELINE}"
