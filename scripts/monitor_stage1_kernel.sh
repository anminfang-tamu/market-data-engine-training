#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EVIDENCE_DIR="${ROOT_DIR}/evidence"

RAW1="${1:-}"
RAW2="${2:-}"
RAW3="${3:-}"
DURATION="${DURATION:-10}"
PERF_FREQ="${PERF_FREQ:-199}"
SUDO="${SUDO:-sudo}"

PID=""
PORT="8888"
IFACE=""

if [[ -z "${RAW1}" ]]; then
  PID="$(pgrep -n engine_app || true)"
elif [[ -d "/proc/${RAW1}" ]]; then
  PID="${RAW1}"
  PORT="${RAW2:-8888}"
  IFACE="${RAW3:-}"
else
  PID="$(pgrep -n engine_app || true)"
  PORT="${RAW1}"
  IFACE="${RAW2:-}"
fi

if [[ -z "${PID}" ]]; then
  echo "usage: $0 [pid] [udp_port] [iface]" >&2
  echo "example: sudo DURATION=15 $0 12345 8888 eth0" >&2
  echo "or: sudo DURATION=15 $0 8888 ens6    # auto-detect latest engine_app pid" >&2
  echo "or: sudo DURATION=15 $0              # auto-detect pid, use defaults" >&2
  exit 1
fi

if [[ ! -d "/proc/${PID}" ]]; then
  echo "pid not found: ${PID}" >&2
  exit 1
fi

STAMP="$(date +%Y%m%d_%H%M%S)"
OUT_DIR="${EVIDENCE_DIR}/${STAMP}_stage1_kernel_udp"
mkdir -p "${OUT_DIR}"

run_capture() {
  local name="$1"
  shift
  bash -lc "$*" > "${OUT_DIR}/${name}.txt" 2>&1 || true
}

run_capture_priv() {
  local name="$1"
  shift
  if [[ -n "${SUDO}" ]]; then
    bash -lc "${SUDO} $*" > "${OUT_DIR}/${name}.txt" 2>&1 || true
  else
    bash -lc "$*" > "${OUT_DIR}/${name}.txt" 2>&1 || true
  fi
}

echo "pid=${PID}" > "${OUT_DIR}/meta.txt"
echo "port=${PORT}" >> "${OUT_DIR}/meta.txt"
echo "iface=${IFACE}" >> "${OUT_DIR}/meta.txt"
echo "duration=${DURATION}" >> "${OUT_DIR}/meta.txt"
echo "perf_freq=${PERF_FREQ}" >> "${OUT_DIR}/meta.txt"
echo "host=$(hostname)" >> "${OUT_DIR}/meta.txt"
echo "started_at=$(date --iso-8601=seconds)" >> "${OUT_DIR}/meta.txt"

run_capture system_uname "uname -a"
run_capture system_lscpu "command -v lscpu >/dev/null && lscpu"
run_capture system_numactl "command -v numactl >/dev/null && numactl --hardware"
run_capture process_status "cat /proc/${PID}/status"
run_capture process_sched "cat /proc/${PID}/sched"
run_capture process_maps "cat /proc/${PID}/maps"
run_capture process_numa_maps "cat /proc/${PID}/numa_maps"
run_capture process_threads "ps -L -p ${PID} -o pid,tid,psr,pcpu,stat,comm"
run_capture process_taskset "command -v taskset >/dev/null && taskset -pc ${PID}"
run_capture perf_event_sources "ls -1 /sys/bus/event_source/devices 2>/dev/null"
run_capture_priv perf_list_brief "command -v perf >/dev/null && perf list --no-desc | sed -n '1,200p'"

run_capture sockstat_before "cat /proc/net/sockstat /proc/net/sockstat6"
run_capture softnet_before "cat /proc/net/softnet_stat"
run_capture softirqs_before "cat /proc/softirqs"
run_capture interrupts_before "cat /proc/interrupts"
run_capture net_udp_before "cat /proc/net/udp /proc/net/udp6"
run_capture ss_udp_before "ss -u -n -i -m -p | grep -E '(:${PORT}\\b|pid=${PID},)' || true"

if [[ -n "${IFACE}" ]]; then
  run_capture iface_link_before "ip -s link show dev ${IFACE}"
  run_capture_priv iface_ethtool_before "command -v ethtool >/dev/null && ethtool -S ${IFACE}"
fi

run_capture pidstat_threads "command -v pidstat >/dev/null && pidstat -t -p ${PID} 1 ${DURATION}"

# Keep the baseline capture portable. `perf stat -d -d` can pull in topdown
# metrics that are not exposed on some EC2 PMU configurations, which turns the
# basic report into syntax errors instead of useful counters.
run_capture_priv perf_stat_basic \
  "perf stat -e task-clock,context-switches,cpu-migrations,page-faults -p ${PID} -- sleep ${DURATION}"

run_capture_priv perf_stat_events \
  "perf stat -e task-clock,cycles,instructions,branches,branch-misses,cache-references,cache-misses,context-switches,cpu-migrations,page-faults,minor-faults,major-faults -p ${PID} -- sleep ${DURATION}"

run_capture_priv perf_stat_syscalls \
  "perf stat -e syscalls:sys_enter_recvmmsg,syscalls:sys_exit_recvmmsg,syscalls:sys_enter_recvmsg,syscalls:sys_exit_recvmsg,syscalls:sys_enter_epoll_wait,syscalls:sys_exit_epoll_wait -p ${PID} -- sleep ${DURATION}"

run_capture_priv perf_record_callgraph \
  "perf record -g -F ${PERF_FREQ} -p ${PID} -- sleep ${DURATION}"

if [[ -f perf.data ]]; then
  mv perf.data "${OUT_DIR}/perf.data" || true
fi
if [[ -f perf.data.old ]]; then
  mv perf.data.old "${OUT_DIR}/perf.data.old" || true
fi
run_capture_priv perf_report_stdio \
  "test -f '${OUT_DIR}/perf.data' && perf report --stdio -i '${OUT_DIR}/perf.data'"

run_capture_priv strace_recv_path \
  "timeout -s INT ${DURATION} strace -ttt -T -f -e trace=epoll_wait,recvmmsg,recvmsg -p ${PID}"

run_capture sockstat_after "cat /proc/net/sockstat /proc/net/sockstat6"
run_capture softnet_after "cat /proc/net/softnet_stat"
run_capture softirqs_after "cat /proc/softirqs"
run_capture interrupts_after "cat /proc/interrupts"
run_capture net_udp_after "cat /proc/net/udp /proc/net/udp6"
run_capture ss_udp_after "ss -u -n -i -m -p | grep -E '(:${PORT}\\b|pid=${PID},)' || true"

if [[ -n "${IFACE}" ]]; then
  run_capture iface_link_after "ip -s link show dev ${IFACE}"
  run_capture_priv iface_ethtool_after "command -v ethtool >/dev/null && ethtool -S ${IFACE}"
fi

echo "finished_at=$(date --iso-8601=seconds)" >> "${OUT_DIR}/meta.txt"
echo "evidence_dir=${OUT_DIR}"
