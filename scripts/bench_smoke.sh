#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/debug}"
ENGINE_BIN="${ENGINE_BIN:-${BUILD_DIR}/src/engine/engine_app}"
GENERATOR_BIN="${GENERATOR_BIN:-${BUILD_DIR}/src/generator/generator_app}"

if [[ ! -x "${ENGINE_BIN}" ]]; then
  echo "engine binary not found: ${ENGINE_BIN}" >&2
  exit 1
fi

if [[ ! -x "${GENERATOR_BIN}" ]]; then
  echo "generator binary not found: ${GENERATOR_BIN}" >&2
  exit 1
fi

"${ENGINE_BIN}" > /tmp/engine_smoke.log 2>&1 &
ENGINE_PID=$!

sleep 0.2

"${GENERATOR_BIN}" > /tmp/generator_smoke.log 2>&1

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
"${SCRIPT_DIR}/collect_evidence.sh" "${ENGINE_PID}" "smoke"

kill "${ENGINE_PID}" || true
wait "${ENGINE_PID}" || true

echo "engine_log=/tmp/engine_smoke.log"
echo "generator_log=/tmp/generator_smoke.log"
