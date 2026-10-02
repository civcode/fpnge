#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <aarch64-binary>" >&2
  exit 2
fi

binary="$1"
machine="$(uname -m)"
if [[ "${machine}" != "aarch64" && "${machine}" != "arm64" ]]; then
  echo "This check must run on an AArch64 host; got ${machine}." >&2
  exit 2
fi

if ! command -v objdump >/dev/null 2>&1; then
  echo "objdump is required." >&2
  exit 2
fi

tmp="$(mktemp)"
trap 'rm -f "${tmp}"' EXIT
objdump -d "${binary}" > "${tmp}"

if grep -Eiq '(^|[[:space:]])(crc32[bchwx]*|pmull2?)([[:space:]]|$)' "${tmp}"; then
  echo "Generic ARMv8-A binary contains optional CRC32/PMULL instructions:" >&2
  grep -Ein '(^|[[:space:]])(crc32[bchwx]*|pmull2?)([[:space:]]|$)' "${tmp}" | head -n 20 >&2
  exit 1
fi

echo "AArch64 baseline ISA check passed: no CRC32/PMULL instructions found."
