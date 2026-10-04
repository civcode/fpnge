#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMMIT="HEAD"
OUTPUT=""

usage() {
  cat <<'EOF'
Usage: ./scripts/package_source.sh [--commit REV] [--output FILE]

Creates a deterministic source tarball from an immutable Git revision using
git archive plus gzip -n, and writes FILE.sha256 beside it.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --commit) COMMIT="${2:?missing value after --commit}"; shift 2 ;;
    --commit=*) COMMIT="${1#*=}"; shift ;;
    --output) OUTPUT="${2:?missing value after --output}"; shift 2 ;;
    --output=*) OUTPUT="${1#*=}"; shift ;;
    --help|-h) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

SHA="$(git -C "${ROOT_DIR}" rev-parse "${COMMIT}^{commit}")"
SHORT="${SHA:0:12}"
if [[ -z "${OUTPUT}" ]]; then
  OUTPUT="${ROOT_DIR}/dist/fpnge-${SHORT}.tar.gz"
fi
mkdir -p "$(dirname "${OUTPUT}")"

TMP_TAR="$(mktemp)"
trap 'rm -f "${TMP_TAR}"' EXIT

git -C "${ROOT_DIR}" archive   --format=tar   --prefix="fpnge-${SHA}/"   "${SHA}" > "${TMP_TAR}"

gzip -n -9 < "${TMP_TAR}" > "${OUTPUT}"
if command -v sha256sum >/dev/null 2>&1; then
  SHA256="$(sha256sum "${OUTPUT}" | awk '{print $1}')"
else
  SHA256="$(shasum -a 256 "${OUTPUT}" | awk '{print $1}')"
fi
printf '%s  %s\n' "${SHA256}" "$(basename "${OUTPUT}")" > "${OUTPUT}.sha256"

printf 'commit=%s\n' "${SHA}"
printf 'archive=%s\n' "${OUTPUT}"
printf 'sha256=%s\n' "${SHA256}"
