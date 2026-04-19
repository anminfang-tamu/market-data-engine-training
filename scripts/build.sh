#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"

lowercase() {
  printf '%s' "$1" | tr '[:upper:]' '[:lower:]'
}

require_cmd() {
  local cmd="$1"
  local message="$2"
  if ! command -v "${cmd}" >/dev/null 2>&1; then
    echo "${message}" >&2
    exit 1
  fi
}

select_generator() {
  if [[ -n "${CMAKE_GENERATOR:-}" ]]; then
    printf '%s\n' "${CMAKE_GENERATOR}"
    return 0
  fi

  if command -v ninja >/dev/null 2>&1; then
    printf '%s\n' "Ninja"
    return 0
  fi

  printf '%s\n' "Unix Makefiles"
}

require_cmd cmake "cmake is required to build this project."

DEFAULT_BUILD_DIR="${ROOT_DIR}/build/$(lowercase "${BUILD_TYPE}")"
BUILD_DIR="${BUILD_DIR:-${DEFAULT_BUILD_DIR}}"

CMAKE_GENERATOR="$(select_generator)"
case "${CMAKE_GENERATOR}" in
  Ninja)
    require_cmd ninja "CMake generator 'Ninja' was selected, but the 'ninja' command is not available."
    ;;
  "Unix Makefiles")
    require_cmd make "CMake generator 'Unix Makefiles' was selected, but the 'make' command is not available."
    ;;
esac

require_cmd pkg-config "pkg-config is required so CMake can locate libdpdk."
if ! pkg-config --exists libdpdk; then
  cat >&2 <<'EOF'
libdpdk was not found through pkg-config.

This project's CMake configuration requires DPDK development files to be installed
and visible to pkg-config before building.

If DPDK is already installed in a non-standard prefix, export PKG_CONFIG_PATH so
pkg-config can find libdpdk.pc and rerun this script.
EOF
  exit 1
fi

echo "Configuring ${BUILD_TYPE} build in ${BUILD_DIR} with generator ${CMAKE_GENERATOR}" >&2
cmake -S "${ROOT_DIR}" \
  -B "${BUILD_DIR}" \
  -G "${CMAKE_GENERATOR}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

cmake --build "${BUILD_DIR}" -j"${JOBS}"
