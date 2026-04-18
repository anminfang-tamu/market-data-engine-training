#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/debug}"
ENGINE_BIN="${ENGINE_BIN:-${BUILD_DIR}/src/engine_app}"

if [[ ! -x "${ENGINE_BIN}" ]]; then
  echo "engine binary not found: ${ENGINE_BIN}" >&2
  exit 1
fi

if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists libdpdk; then
  DPDK_LIBDIR="$(pkg-config --variable=libdir libdpdk)"
  if [[ -n "${DPDK_LIBDIR}" && -d "${DPDK_LIBDIR}" ]]; then
    export LD_LIBRARY_PATH="${DPDK_LIBDIR}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
  fi
fi

"${ENGINE_BIN}" "$@"
