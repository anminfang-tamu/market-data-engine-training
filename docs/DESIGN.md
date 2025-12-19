FIX Engine Rebuild (Core C++ / Linux Training Project)
Objective

In a Linux (AWS EC2) environment, hand-build a long-running, observable, stress-testable, and postmortem-driven
Market Data Processing Engine, in order to systematically strengthen:

C++ engineering skills (memory management, object lifetime, concurrency)

Linux runtime understanding (CPU, memory, syscalls)

Performance analysis and incident postmortem skills

This is not a demo project, but a production-environment simulation training ground.

1. Environment & Constraints
   1.1 Runtime Environment

Platform: AWS EC2

OS: Ubuntu 22.04 LTS (or Amazon Linux 2023)

Instance: c6i.large / c7i.large

Tooling:

gdb

perf

strace

pmap

pidstat

htop

Sanitizers (asan, ubsan)

1.2 Mandatory Principles

All code must run on Linux

The engine is a long-running process

Every stress test must produce reproducible evidence

Do not aim to fully implement FIX — prioritize data flow and engineering fundamentals

2. System Architecture (Single-Machine Phase)
   +---------------------+ +----------------------+
   | feed_generator | -----> | market_engine |
   | (external process) | TCP | (long-running) |
   +---------------------+ +----------------------+

Initial phase: same EC2 instance, separate processes

Later phase (after maturity): split across two machines to validate real network jitter

3. feed_generator (Market Data Simulator)
   3.1 Responsibilities

Continuously send market data to the engine

Deterministically generate load, anomalies, and failures

3.2 Message Format (Binary, Simplified)
struct MarketDataMsg {
uint32_t symbol_id;
int64_t exchange_ts;
int64_t bid_price;
int64_t ask_price;
uint32_t bid_size;
uint32_t ask_size;
};

Fixed size

Network byte order

No FIX protocol complexity

3.3 Configurable Parameters (Command Line)

--rate – message rate (msg/sec)

--burst-rate – burst throughput

--burst-duration

--drop-ratio – packet loss ratio

--dup-ratio – duplicate ratio

--disorder-ratio – out-of-order ratio

--symbol-count

--seed – random seed (must be reproducible)

4. market_engine (Core Training Target)
   4.1 Processing Pipeline (Mandatory Segmentation)
   [ socket recv ]
   |
   v
   [ ingest / parse ]
   |
   v
   [ normalize ]
   |
   v
   [ process (strategy mock) ]
   |
   v
   [ act (order mock / logging) ]

Each stage must have independent timing and metrics.

4.2 Threading Model (Initial Recommendation)

Thread 1: network ingest

Thread 2: processing / strategy

Thread 3: logging / output (optional)

Communication:

Bounded queues

Explicit backpressure strategy

4.3 Mandatory Metrics (Real-Time)

Throughput (msg/sec)

Latency histogram: p50 / p95 / p99

Queue depth

Drop count

Backpressure trigger count

RSS / heap size (periodic sampling)

5. Linux Observability (Mandatory)
   5.1 Evidence Required for Every Stress Test

pidstat -p <pid> 1

pmap -x <pid>

perf stat -p <pid>

perf record + perf report

strace -p <pid> -c

Engine-reported metrics

No evidence = the test is invalid

6. Stress Testing & Incident Injection
   6.1 Required Stress Scenarios

High-rate bursts (10×–100×)

Out-of-order messages

Duplicate messages

Packet loss (1% / 5% / 20%)

Slow consumers (intentional sleep / blocking)

IO jitter (slow logging)

6.2 Required Failure Types

Unbounded memory growth

p99 latency explosion

Silent data corruption

Backpressure design failure

Performance degradation after long-running execution

7. Postmortem Template
   Incident Summary

What happened:

When detected:

Impact:

Evidence

Metrics snapshot:

perf / strace / pmap evidence:

Root Cause

Immediate cause:

Deeper design flaw:

Fix

Code changes:

Why this prevents recurrence:

Follow-up

Additional monitoring:

Tests added:

8. Four-Week Validation Milestones
   Week 1

Engine runs for 30 minutes with no memory growth

Metrics output functioning

Week 2

Complete one burst stress test

Optimize one real hotspot using perf (before/after comparison required)

Week 3

Intentionally trigger an incident

Produce a complete postmortem

Week 4

Backpressure strategy clearly defined

Stable 24-hour soak test

9. Design Philosophy (For Future Reference)

Correctness > elegant architecture

Observability > raw throughput numbers

Reproducible failures > “rare issues”

Explicit trade-offs > perfection fantasy

10. When to Introduce FIX / Multi-Host Deployment

Only allowed when:

Single-machine runs stably for 24 hours

p99 latency is explainable

No unbounded memory growth

Every incident is reproducible and postmortem-ready

Then (and only then):

Introduce FIX session layer

Split generator and engine across two EC2 instances
