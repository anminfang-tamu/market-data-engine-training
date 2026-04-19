#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/debug}"
ENGINE_BIN="${ENGINE_BIN:-${BUILD_DIR}/src/engine_app}"
DPDK_HUGE_DIR="${DPDK_HUGE_DIR:-/mnt/huge}"

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

has_arg() {
  local needle="$1"
  shift
  local arg
  for arg in "$@"; do
    if [[ "${arg}" == "${needle}" ]]; then
      return 0
    fi
  done
  return 1
}

explicit_option_value() {
  local option="$1"
  shift
  local prev=""
  local arg
  for arg in "$@"; do
    if [[ "${prev}" == "${option}" ]]; then
      printf '%s\n' "${arg}"
      return 0
    fi
    case "${arg}" in
      "${option}"=*)
        printf '%s\n' "${arg#*=}"
        return 0
        ;;
    esac
    prev="${arg}"
  done
  return 1
}

explicit_huge_dir() {
  explicit_option_value "--huge-dir" "$@"
}

explicit_file_prefix() {
  explicit_option_value "--file-prefix" "$@"
}

hugepages_total() {
  awk '/^HugePages_Total:/ { print $2 }' /proc/meminfo
}

any_hugetlbfs_mount() {
  awk '$3 == "hugetlbfs" { found=1 } END { exit(found ? 0 : 1) }' /proc/mounts
}

vfio_noiommu_enabled() {
  local noiommu_path="/sys/module/vfio/parameters/enable_unsafe_noiommu_mode"
  [[ -r "${noiommu_path}" ]] || return 1

  local value
  value="$(<"${noiommu_path}")"
  [[ "${value}" == "Y" || "${value}" == "1" ]]
}

has_vfio_pci_netdev() {
  local dev
  local class
  local driver

  for dev in /sys/bus/pci/devices/*; do
    [[ -r "${dev}/class" && -L "${dev}/driver" ]] || continue
    class="$(<"${dev}/class")"
    [[ "${class}" == "0x020000" ]] || continue
    driver="$(basename "$(readlink "${dev}/driver")")"
    [[ "${driver}" == "vfio-pci" ]] && return 0
  done

  return 1
}

default_iova_mode() {
  if [[ -n "${DPDK_IOVA_MODE:-}" ]]; then
    printf '%s\n' "${DPDK_IOVA_MODE}"
    return 0
  fi

  if vfio_noiommu_enabled && has_vfio_pci_netdev; then
    if [[ "${EUID}" -eq 0 ]]; then
      printf '%s\n' "pa"
      return 0
    fi

    cat >&2 <<EOF
Detected a network device bound to vfio-pci while VFIO no-IOMMU mode is enabled.
That setup requires IOVA=PA, but physical addresses are only available to a
privileged process on current Linux kernels.

Run the engine as root:
  sudo ./scripts/run_engine.sh --iova-mode=pa

Alternative:
  grant ${ENGINE_BIN} the capabilities required to read /proc/self/pagemap and
  lock hugepages in memory.
EOF
    exit 1
  fi

  printf '%s\n' "va"
}

fail_hugepage_setup() {
  local reason="$1"
  cat >&2 <<EOF
${reason}

DPDK EAL needs reserved hugepages before the engine can start with a physical NIC.
Recommended fix on Ubuntu EC2:
  sudo mount -t hugetlbfs -o remount,uid=$(id -u),gid=$(id -g),mode=1770 nodev ${DPDK_HUGE_DIR}

If remount succeeds but ${DPDK_HUGE_DIR} is still not writable, recreate the mount:
  sudo umount ${DPDK_HUGE_DIR}
  sudo mount -t hugetlbfs -o uid=$(id -u),gid=$(id -g),mode=1770 nodev ${DPDK_HUGE_DIR}

Manual check:
  grep '^HugePages_' /proc/meminfo
  mount | grep hugetlbfs
  stat -c '%A %u:%g %n' ${DPDK_HUGE_DIR}
EOF
  exit 1
}

ENGINE_ARGS=()
EXPLICIT_IOVA_MODE="$(explicit_option_value "--iova-mode" "$@" || true)"
EXPLICIT_FILE_PREFIX="$(explicit_file_prefix "$@" || true)"
DPDK_FILE_PREFIX="${DPDK_FILE_PREFIX:-md-engine}"
SELECTED_IOVA_MODE="${EXPLICIT_IOVA_MODE:-$(default_iova_mode)}"

if [[ -z "${EXPLICIT_IOVA_MODE}" && -n "${SELECTED_IOVA_MODE}" ]]; then
  ENGINE_ARGS+=(--iova-mode "${SELECTED_IOVA_MODE}")
fi

if [[ -z "${EXPLICIT_FILE_PREFIX}" && -n "${DPDK_FILE_PREFIX}" ]]; then
  ENGINE_ARGS+=(--file-prefix "${DPDK_FILE_PREFIX}")
fi

if ! has_arg "--no-huge" "$@"; then
  HP_TOTAL="$(hugepages_total)"
  if [[ -z "${HP_TOTAL}" || "${HP_TOTAL}" == "0" ]]; then
    fail_hugepage_setup "No hugepages are currently reserved."
  fi

  EXPLICIT_HUGE_DIR="$(explicit_huge_dir "$@" || true)"
  if [[ -n "${EXPLICIT_HUGE_DIR}" ]]; then
    if ! mountpoint -q "${EXPLICIT_HUGE_DIR}"; then
      fail_hugepage_setup "Requested --huge-dir '${EXPLICIT_HUGE_DIR}' is not a hugetlbfs mountpoint."
    fi
    if [[ ! -w "${EXPLICIT_HUGE_DIR}" ]]; then
      fail_hugepage_setup "Requested --huge-dir '${EXPLICIT_HUGE_DIR}' is not writable by user '$(id -un)'. Remount hugetlbfs with uid=$(id -u),gid=$(id -g),mode=1770 or choose a writable hugepage mount."
    fi
  elif ! has_arg "--in-memory" "$@"; then
    if mountpoint -q "${DPDK_HUGE_DIR}"; then
      if [[ ! -w "${DPDK_HUGE_DIR}" ]]; then
        fail_hugepage_setup "Hugetlbfs mount '${DPDK_HUGE_DIR}' is not writable by user '$(id -un)'. Remount it with uid=$(id -u),gid=$(id -g),mode=1770."
      fi
      ENGINE_ARGS+=(--huge-dir "${DPDK_HUGE_DIR}")
    elif ! any_hugetlbfs_mount; then
      fail_hugepage_setup "No hugetlbfs mount is available for DPDK hugepage files."
    fi
  fi
fi

echo "engine dpdk_port=${MD_ENGINE_DPDK_PORT_ID:-0} queue=${MD_ENGINE_DPDK_QUEUE_ID:-0} iova_mode=${SELECTED_IOVA_MODE:-default} file_prefix=${EXPLICIT_FILE_PREFIX:-${DPDK_FILE_PREFIX:-default}}" >&2

"${ENGINE_BIN}" "${ENGINE_ARGS[@]}" "$@"
