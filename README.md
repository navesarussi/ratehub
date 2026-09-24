# ratehub

Multi-process telemetry concentrator in C++20. Four POSIX processes share one
memory mapping. The hot path uses no locks and no heap allocation: an SPSC
ring, per-source seqlocks, and a triple-buffered fleet snapshot.

Production background behind the author is **C** on hard real-time avionics.
This repository is a **personal** systems project. C++ owns resources; the hot
loop is plain records and arrays.

## Architecture

```
replay file
    → ingest (2 threads: read bytes, decode frames, mutex queue)
    → SPSC ring (shared mapping)
    → compute (5 ms / 25 ms / 100 ms threads + watchdog)
         SoA integrate → seqlock per source → window seqlock → triple buffer
    → publish (copy consistent snapshot to a file)
    ↑
supervisor (spawn, heartbeat, restart compute, cooperative shutdown)
```

External readers never map the compute region. They receive a finished copy.

## What it demonstrates

| Area | Mechanism |
|------|-----------|
| IPC | `shm_open` + `mmap`, versioned schema, generation on restart |
| Concurrency | SPSC ring, seqlock, triple buffer; mutex only on ingest cold path |
| Memory model | acquire/release publication edges; payload as relaxed atomics in seqlock |
| Cache | Heartbeats and seqlock slots on separate 64-byte lines; `ratehub_bench` measures false sharing and L2-resident vs DRAM-strided walks |
| Determinism | Fixed-point `int32` positions (mm), `int64` accumulators, no float in the hot loop |
| Operations | Soft deadlines with overrun counters; watchdog restarts stuck compute |

Full contract: [`docs/DESIGN.md`](docs/DESIGN.md). Interview file map:
[`docs/INTERVIEW.md`](docs/INTERVIEW.md).

## Quick start

```bash
cmake -S . -B build -DRATEHUB_SANITIZER=address
cmake --build build
./build/ratehub_tests

cmake --build build --target make_replay
./build/make_replay replay.bin
./build/ratehub run replay.bin snapshot.txt
cat snapshot.txt
```

Expected output after two 5 ms integration steps at 2000 mm/s:

```
generation 1
1 1020 0 2
```

ThreadSanitizer build (separate binary — do not combine with ASan):

```bash
cmake -S . -B build-tsan -DRATEHUB_SANITIZER=thread
cmake --build build-tsan
./build-tsan/ratehub_tests
```

Benchmark (not a CI gate; numbers are machine-specific):

```bash
cmake --build build --target ratehub_bench
./build/ratehub_bench
```

Set `RATEHUB_PACE=0` to skip period sleeps (used by the test suite).

## Wire format

38-byte big-endian frames, CRC-16/CCITT-FALSE. See offset table in
[`include/ratehub/frame.hpp`](include/ratehub/frame.hpp).

## Layout

| Path | Role |
|------|------|
| `docs/DESIGN.md` | System contract |
| `docs/INTERVIEW.md` | Concept → file map for screens |
| `include/ratehub/*.hpp` | Contracts in header comments |
| `src/supervisor.cpp` | Process orchestration |
| `src/compute.cpp` | Paced threads + watchdog |
| `src/bench.cpp` | Cache hierarchy measurements |

Each header comment block states who may call the type, what failure leaves
unchanged, and which memory order is the publication edge.

## CI

GitHub Actions runs ASan+UBSan and TSan on Ubuntu. See
[`.github/workflows/ci.yml`](.github/workflows/ci.yml).

## License

MIT — see repository license file if present; otherwise treat as portfolio
code for review in interviews.
