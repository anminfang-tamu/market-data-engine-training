#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ -n "${BUILD_DIR:-}" ]]; then
  SELECTED_BUILD_DIR="${BUILD_DIR}"
elif [[ -x "${ROOT_DIR}/build/debug/src/generator_app" ]]; then
  SELECTED_BUILD_DIR="${ROOT_DIR}/build/debug"
elif [[ -x "${ROOT_DIR}/build/generator-debug/src/generator_app" ]]; then
  SELECTED_BUILD_DIR="${ROOT_DIR}/build/generator-debug"
else
  SELECTED_BUILD_DIR="${ROOT_DIR}/build/generator-debug"
fi

GENERATOR_BIN="${GENERATOR_BIN:-${SELECTED_BUILD_DIR}/src/generator_app}"

if [[ ! -x "${GENERATOR_BIN}" ]]; then
  echo "generator binary not found: ${GENERATOR_BIN}" >&2
  exit 1
fi

"${GENERATOR_BIN}"
