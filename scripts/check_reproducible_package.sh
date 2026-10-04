#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT

A="${TMP_DIR}/a.tar.gz"
B="${TMP_DIR}/b.tar.gz"
"${ROOT_DIR}/scripts/package_source.sh" --commit HEAD --output "${A}" >/dev/null
"${ROOT_DIR}/scripts/package_source.sh" --commit HEAD --output "${B}" >/dev/null

cmp "${A}" "${B}"

for required in fpnge.cc fpnge.h LICENSE README.md docs/m6_validation.md; do
  if ! tar -tzf "${A}" | grep -Eq "/${required//./\.}$"; then
    echo "Package missing required file: ${required}" >&2
    exit 1
  fi
done

printf 'Reproducible source package check passed.\n'
