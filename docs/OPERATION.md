# Stage 1 Kernel UDP Monitoring

Use `scripts/monitor_stage1_kernel.sh` on Linux to collect evidence around the
kernel UDP path for the engine process.

Example:

```bash
PID=$(pgrep -n engine_app)
sudo DURATION=15 PERF_FREQ=499 ./scripts/monitor_stage1_kernel.sh <engine_pid> 8888 ens5
```

Arguments:

- `pid`: engine process id
- `udp_port`: local UDP port, default `8888`
- `iface`: optional NIC name such as `eth0` or `ens5`

Outputs are written under `evidence/<timestamp>_stage1_kernel_udp/`.

Collected evidence includes:

- process/thread placement and scheduler state
- `pidstat` thread CPU samples
- `perf stat` baseline scheduler/software counters, plus hardware counters when exposed by the host PMU
- `perf record` call graph samples
- `strace` timing for `epoll_wait`, `recvmmsg`, and `recvmsg`
- socket memory state via `ss`
- kernel network pressure snapshots from `/proc/net/softnet_stat`
- interrupt and softirq snapshots
- NIC counters from `ethtool -S` when an interface is provided

Notes:

- This gives you process-level and kernel-path evidence, not true wire-to-user
  nanosecond latency.
- Some EC2 instance and kernel combinations do not expose hardware PMU events to
  the guest. In that case `task-clock` and syscall/scheduler counters remain
  useful, while `cycles`, `instructions`, cache, and topdown metrics may show as
  unsupported.
- To get true packet arrival timestamps later, add hardware timestamping or
  in-process timestamp probes.
- `perf`, `strace`, and `ethtool -S` may require root privileges.
