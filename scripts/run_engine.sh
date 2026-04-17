#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/debug}"
ENGINE_BIN="${ENGINE_BIN:-${BUILD_DIR}/src/engine_app}"

if [[ ! -x "${ENGINE_BIN}" ]]; then
  echo "engine binary not found: ${ENGINE_BIN}" >&2
  exit 1
fi

"${ENGINE_BIN}" "$@"
