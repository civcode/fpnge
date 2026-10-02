#!/bin/bash -e
# Copyright 2021 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    https:#www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -o pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/build_profiles.sh
source "${ROOT_DIR}/scripts/build_profiles.sh"

PROFILE="${FPNGE_BUILD_PROFILE:-native}"
EXTRA_CXXFLAGS=()

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
    --)
      shift
      EXTRA_CXXFLAGS+=("$@")
      break
      ;;
    *)
      EXTRA_CXXFLAGS+=("$1")
      shift
      ;;
  esac
done

fpnge_set_build_profile "${PROFILE}"

mkdir -p "${ROOT_DIR}/build"
cd "${ROOT_DIR}/build"

CXX="${CXX:-clang++}"

[ -f lodepng.cpp ] || wget https://raw.githubusercontent.com/lvandeve/lodepng/8c6a9e30576f07bf470ad6f09458a2dcd7a6a84a/lodepng.cpp
[ -f lodepng.h ] || wget https://raw.githubusercontent.com/lvandeve/lodepng/8c6a9e30576f07bf470ad6f09458a2dcd7a6a84a/lodepng.h
[ -f lodepng.o ] || "${CXX}" lodepng.cpp -O3 -o lodepng.o -c

"${CXX}" -O3 -g -Wall \
  "${FPNGE_PROFILE_CXXFLAGS[@]}" \
  "${EXTRA_CXXFLAGS[@]}" \
  -I. lodepng.o \
  ../fpnge.cc ../fpnge_main.cc \
  -o fpnge
