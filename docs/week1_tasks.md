# Week 0-1: EC2 Bootstrap + Toolchain + Build System Acceptance Checklist (Ubuntu 22.04)

## A. EC2 Basics and Security (One-Time)

### A1. Update System and Core Tools

```bash
sudo apt-get update
sudo apt-get -y upgrade
sudo apt-get -y install git curl wget unzip zip ca-certificates gnupg lsb-release
```

Validation

- `git --version` prints output.
- `uname -a` shows Ubuntu 22.04 kernel info.

## B. Build Toolchain (GCC + CMake + Ninja)

### B1. Install Build Tools

```bash
sudo apt-get -y install build-essential cmake ninja-build pkg-config
```

Validation

```bash
gcc --version
g++ --version
cmake --version
ninja --version
```

All four must succeed.

## C. Linux Observability Tools (Core Training Focus)

### C1. Install Baseline Observability Tools

```bash
sudo apt-get -y install \
  gdb \
  strace \
  linux-tools-common linux-tools-generic \
  psmisc \
  sysstat \
  procps \
  lsof \
  time
```

Notes

- `sysstat` provides `pidstat`.
- `procps` provides `pmap`.
- `linux-tools-generic` + `linux-tools-common` provide `perf` (common Ubuntu path).

Validation

```bash
gdb --version
strace -V
pidstat -V
pmap -V
perf --version
```

All must succeed.

Gotcha: `perf` may report permissions or kernel restrictions. Do not skip; fix it below.

## D. Make `perf` Usable (Otherwise Training Is Fake)

### D1. Relax `perf_event` Permissions (Temporary)

```bash
cat /proc/sys/kernel/perf_event_paranoid
```

If the output is `3` or `2`, you may be restricted.

Temporary (until reboot):

```bash
sudo sysctl -w kernel.perf_event_paranoid=1
sudo sysctl -w kernel.kptr_restrict=0
```

Persistent (write config):

```bash
echo "kernel.perf_event_paranoid=1" | sudo tee /etc/sysctl.d/99-perf.conf
echo "kernel.kptr_restrict=0" | sudo tee -a /etc/sysctl.d/99-perf.conf
sudo sysctl --system
```

Validation (critical)

Run a quick self-test:

```bash
perf stat ls >/dev/null
```

Must output stats, not "permission denied".

## E. Core Dump Postmortem Capability (Required)

### E1. Enable Core Dumps

```bash
ulimit -c unlimited
```

Recommended: set core file naming (optional but preferred):

```bash
echo "core.%e.%p.%t" | sudo tee /proc/sys/kernel/core_pattern
```

Validation (critical)

Write a tiny crash program to verify the core + gdb flow (null deref or abort is
fine).

Acceptance Criteria

- A `core.*` file is generated after the crash.
- `gdb <binary> <corefile>` opens and `bt` prints a backtrace.

Gotcha: If you skip this, future "incident review" will be just talk.

## F. Repo Build and Directory Layout (Write Your Own CMake)

Follow your prior repo structure (you already have the directory blueprint). This
week you only need:

### F1. Minimal Build Targets (Two Executables)

- `market_engine`
- `feed_generator`

No functionality required; just build, print one line, and exit.

### F2. CMake Acceptance Commands (All Must Pass)

1. Debug + Ninja

```bash
cmake -S . -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug -j
```

2. Release + Ninja

```bash
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/release -j
```

3. Emit `compile_commands.json` (Required)

```bash
cmake -S . -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
test -f build/debug/compile_commands.json && echo OK
```

Validation: must print `OK`.

## G. Sanitizer Flags (You Must Implement in CMake)

This week only requires ASAN + UBSAN to be toggled on (TSAN later).

### G1. Validation Command

Implement a CMake switch like `-DENABLE_ASAN=ON` (name is up to you), then pass:

```bash
cmake -S . -B build/asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build/asan -j
```

Validation

- Executables run (print one line and exit).
- No missing sanitizer runtime errors.
- You can explain why sanitizer flags must appear in both compile and link steps.

## H. First "Evidence Pack" Directory Convention (Enforced Day One)

Every stress test must produce evidence. In Week 1, build the minimum version
to establish the habit.

### H1. Directory Naming Rule (Required)

```
evidence/
YYYYMMDD_HHMM_<scenario>/
  cmdline.txt
  pidstat.txt
  pmap.txt
  perf_stat.txt
  strace_c.txt
  engine_metrics.log
```

### H2. Week 1 Required Delivery: Collect Evidence on an Idle Engine

Assume engine PID is `$PID`:

```bash
mkdir -p evidence/$(date +%Y%m%d_%H%M)_smoke
cd evidence/$(date +%Y%m%d_%H%M)_smoke

echo "scenario=smoke" > cmdline.txt

pidstat -p $PID 1 5 > pidstat.txt
pmap -x $PID > pmap.txt
perf stat -p $PID sleep 3 2> perf_stat.txt
strace -p $PID -c -f -o strace_c.txt sleep 3
```

Validation

- All five files exist and are non-empty.
- You can read at least these in `perf_stat.txt`: cycles / instructions / task-clock.
- You can explain the most frequent syscall in `strace_c.txt` (even if it is boring).

## Week 1 Final Acceptance: You Must Produce These Six Hard Results

- `perf stat ls` runs without permission errors.
- Core dump + gdb backtrace flow works.
- Debug and Release builds succeed.
- `compile_commands.json` is generated.
- ASAN/UBSAN build runs.
- The first evidence directory is created according to the rules.
