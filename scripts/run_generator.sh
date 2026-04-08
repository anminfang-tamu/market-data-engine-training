#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/debug}"
GENERATOR_BIN="${GENERATOR_BIN:-${BUILD_DIR}/src/generator_app}"

if [[ ! -x "${GENERATOR_BIN}" ]]; then
  echo "generator binary not found: ${GENERATOR_BIN}" >&2
  exit 1
fi

"${GENERATOR_BIN}"
