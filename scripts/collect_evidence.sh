#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EVIDENCE_DIR="${ROOT_DIR}/evidence"

PID="${1:-}"
SCENARIO="${2:-smoke}"
DURATION="${DURATION:-5}"
SUDO="${SUDO:-}"

if [[ -z "${PID}" ]]; then
  echo "usage: $0 <pid> [scenario]" >&2
  exit 1
fi

STAMP="$(date +%Y%m%d_%H%M%S)"
OUT_DIR="${EVIDENCE_DIR}/${STAMP}_${SCENARIO}"

mkdir -p "${OUT_DIR}"
echo "pid=${PID}" > "${OUT_DIR}/cmdline.txt"
echo "scenario=${SCENARIO}" >> "${OUT_DIR}/cmdline.txt"
echo "duration=${DURATION}" >> "${OUT_DIR}/cmdline.txt"

pidstat -p "${PID}" 1 "${DURATION}" > "${OUT_DIR}/pidstat.txt"
pmap -x "${PID}" > "${OUT_DIR}/pmap.txt"

${SUDO} perf stat -e task-clock,context-switches,cpu-migrations,page-faults -p "${PID}" \
  sleep "${DURATION}" 2> "${OUT_DIR}/perf_stat.txt" || true

${SUDO} strace -p "${PID}" -c -f -o "${OUT_DIR}/strace_c.txt" \
  sleep "${DURATION}" || true

echo "evidence_dir=${OUT_DIR}"
