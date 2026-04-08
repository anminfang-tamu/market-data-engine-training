#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/${BUILD_TYPE,,}}"
CMAKE_GENERATOR="${CMAKE_GENERATOR:-Ninja}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"

cmake -S "${ROOT_DIR}" \
  -B "${BUILD_DIR}" \
  -G "${CMAKE_GENERATOR}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

cmake --build "${BUILD_DIR}" -j"${JOBS}"
