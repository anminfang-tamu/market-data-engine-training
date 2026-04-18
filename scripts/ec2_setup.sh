#!/usr/bin/env bash
set -euo pipefail

DPDK_HUGEPAGES_2MB="${DPDK_HUGEPAGES_2MB:-0}"
DPDK_HUGE_DIR="${DPDK_HUGE_DIR:-/mnt/huge}"

sudo yum update -y

sudo yum install -y \
  gcc \
  gcc-c++ \
  make \
  cmake \
  ninja-build \
  git \
  pkgconfig \
  gdb \
  strace \
  numactl \
  numactl-devel \
  perf \
  sysstat \
  procps \
  lsof \
  ethtool \
  pciutils \
  python3 \
  python3-pip

# Install meson and pyelftools via pip (not in yum)
pip3 install meson pyelftools --break-system-packages 2>/dev/null || pip3 install meson pyelftools

# Install DPDK from source (Amazon Linux yum repos don't ship dpdk-dev/libdpdk-dev)
DPDK_VER="23.11"
if ! pkg-config --modversion libdpdk >/dev/null 2>&1; then
  echo "Building DPDK ${DPDK_VER} from source..."
  cd /tmp
  wget -q "https://fast.dpdk.org/rel/dpdk-${DPDK_VER}.tar.xz"
  tar xf "dpdk-${DPDK_VER}.tar.xz"
  cd "dpdk-${DPDK_VER}"
  meson setup build
  cd build
  ninja
  sudo ninja install
  sudo ldconfig
  cd /tmp && rm -rf "dpdk-${DPDK_VER}" "dpdk-${DPDK_VER}.tar.xz"
fi

sudo sysctl -w kernel.perf_event_paranoid=1
sudo sysctl -w kernel.kptr_restrict=0
echo "kernel.perf_event_paranoid=1" | sudo tee /etc/sysctl.d/99-perf.conf >/dev/null
echo "kernel.kptr_restrict=0"      | sudo tee -a /etc/sysctl.d/99-perf.conf >/dev/null

sudo sysctl -w kernel.yama.ptrace_scope=0
echo "kernel.yama.ptrace_scope=0" | sudo tee /etc/sysctl.d/10-ptrace.conf >/dev/null

sudo sysctl --system

if ! sudo modprobe vfio-pci; then
  echo "warning: could not load vfio-pci; keep using kernel networking until the target Linux host exposes VFIO" >&2
fi

if [[ "${DPDK_HUGEPAGES_2MB}" != "0" ]]; then
  echo "${DPDK_HUGEPAGES_2MB}" | sudo tee /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages >/dev/null
  sudo mkdir -p "${DPDK_HUGE_DIR}"
  if ! mountpoint -q "${DPDK_HUGE_DIR}"; then
    sudo mount -t hugetlbfs nodev "${DPDK_HUGE_DIR}"
  fi
fi

pkg-config --modversion libdpdk >/dev/null

echo "DPDK packages are installed. Bind only a dedicated data NIC; do not move the EC2 management interface off its kernel driver."