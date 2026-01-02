#!/usr/bin/env bash
set -euo pipefail

sudo apt update

sudo apt install -y \
  build-essential \
  cmake \
  ninja-build \
  git \
  pkg-config \
  gdb \
  strace \
  linux-tools-common \
  linux-tools-generic \
  sysstat \
  procps \
  lsof

sudo sysctl -w kernel.perf_event_paranoid=1
sudo sysctl -w kernel.kptr_restrict=0
echo "kernel.perf_event_paranoid=1" | sudo tee /etc/sysctl.d/99-perf.conf >/dev/null
echo "kernel.kptr_restrict=0"      | sudo tee -a /etc/sysctl.d/99-perf.conf >/dev/null

sudo sysctl -w kernel.yama.ptrace_scope=0
echo "kernel.yama.ptrace_scope=0" | sudo tee /etc/sysctl.d/10-ptrace.conf >/dev/null

sudo sysctl --system
