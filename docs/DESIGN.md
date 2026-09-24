# ratehub — design

A narrow telemetry concentrator. Fixed-rate computation, one shared-memory
mapping, and a hot path with no locks and no allocation. Linux deadlines are
soft: overruns are counted, not treated as hard real-time.

This document is the contract. Code that disagrees with it is a bug.

## Process model

Four processes. The supervisor is the parent and is single-threaded on purpose.
A second thread there adds shutdown races and does not buy throughput.

| Process | Threads | Role |
|---|---|---|
| Supervisor | 1 | Create the shared mapping, spawn children, read heartbeats, translate a signal into a cooperative shutdown flag, restart a dead child up to a fixed limit |
| Ingest | 2 | Source thread reads bytes. Frame thread validates and decodes. A bounded mutex queue sits between them |
| Compute | 4 | 5 ms fast path, 25 ms window, 100 ms snapshot, watchdog |
| Publish | 2 | Observe a consistent snapshot, hand a finished copy to external readers |

External readers never map the compute region. They receive a finished copy.
A slow reader cannot stall the writer.

The first source is a replay file so a run is deterministic. The source thread
is the seam for a later live input. Live input is not in the first cut.

## Shared mapping

One object, three regions, with a schema version in the header:

1. Control — magic, schema version, heartbeats, shutdown flag.
2. Record ring — ingest produces, the 5 ms thread is the only consumer.
3. Snapshot — compute publishes, publish copies out. A generation counter
   increments when compute restarts, so a new process is not read as a
   continuation of the old ring.

A schema mismatch refuses to start.

## Data flow

Wire records are big-endian. Conversion happens once, in the frame thread,
into a native trivially-copyable record. Parsing copies bytes with `memcpy`.
It does not cast the wire buffer to a wider integer type.

64 sources. The source id is the slot index. Anything outside `0 .. 63` is a
bad record.

Values are `int32` in a fixed scale: millimeters, and millimeters per second.
The hot loop does not use floating point. Accumulators are `int64`. A result
is stored only after every component passes a range check. Signed overflow is
undefined in C++; the code does not rely on wraparound.

Publication, in order:

1. The 5 ms thread updates a private structure-of-arrays table, then publishes
   one per-source slot through a seqlock.
2. The 25 ms thread reads those seqlocks. It does not read the 5 ms working
   memory. It publishes the latest position and the total publish count.
   Positions it did not sample are not reconstructed. A seqlock keeps one
   value, not the history.
3. The 100 ms thread reads the window seqlock only, then publishes one fleet
   snapshot through a triple buffer.
4. Publish copies that snapshot out. Readers see the copy.

A full ring blocks the producer. Records are not dropped under pressure. Loss
is visible only when the supervisor restarts a process, and then only as a
generation gap.

Each rate thread records its own overrun. The watchdog does not cancel
threads. A silent thread sets a fault flag. The supervisor restarts that
process, up to the restart limit, then exits non-zero.

A thread blocked on a full queue also wakes on the shutdown flag.

## Cache

The hot table is four parallel arrays (`pos_x`, `pos_y`, `vel_x`, `vel_y`),
64 `int32` values each, about 1 KB, intended to sit in L1.

Each seqlock slot is aligned to 64 bytes so adjacent sources do not share a
cache line. Each thread's cycle stamp is likewise aligned.

False sharing is measured in a separate benchmark that places counters on
purpose in one line. The product path stays padded.

## Memory ordering

- SPSC ring: the producer finishes the slot, then release-stores the write
  index. The consumer acquire-loads that index, then reads the slot.
- Seqlock: an odd sequence means a write is in progress. Payload words are
  relaxed atomics so the copy is race-free under the C++ memory model. The
  release/acquire pair on the sequence supplies the happens-before edge.
- Triple buffer: one writer, one reader, three slots. `publish` and `consume`
  swap indexes through one atomic, so the two sides never own the same slot.
- Shutdown and heartbeats use acquire/release. `seq_cst` is not required on a
  flag that is checked once per cycle.

## Errors

| Class | Examples | Action |
|---|---|---|
| Bad input | checksum, schema, source id, value out of range | drop the record, increment a counter, keep running |
| Backpressure | ingest queue or ring full | block the producer; wake on shutdown |
| Budget | a cycle missed its period | count an overrun; do not kill the thread |
| Structural | schema mismatch, shm failure, restart limit | supervisor stops every child and exits non-zero |

A stale value may be retained only with an explicit age. The code does not
invent a fresh sample.

## Tests

- Unit tests, no threads, manual clock: framing, checksum, endian, range,
  structure-of-arrays update, `int64` narrowing.
- ThreadSanitizer: SPSC ring, seqlock (no torn value), triple buffer (only
  complete snapshots), a blocked producer that wakes on shutdown.
- Process tests: schema mismatch refuses to start, a dead child bumps the
  generation, exceeding the restart limit exits non-zero, a replay file
  yields an expected snapshot.

CI runs AddressSanitizer and UndefinedBehaviorSanitizer in one build, and
ThreadSanitizer in another. They are not combined. Benchmarks are not a gate.

## What this repository is not

It is not a MIL-STD-1553 stack, an RTOS, a kernel module, or a distributed
store. The README states that production experience behind the author is C,
and that this repository is a personal C++ systems project.

## Status

Implemented and tested: wire frame, fixed-point integration, SPSC ring,
seqlock, triple buffer, blocking queue, shared mapping, ingest, compute,
publish, and the supervisor. A replay of two records produces position
1020 mm and publish count 2. A schema mismatch refuses to attach. One
compute crash restarts and bumps the generation. A third crash exits
non-zero.

Each rate thread beats a steady-clock timestamp on its own cache line, does
its work, then sleeps the rest of its period. Work past the period increments
`overrun_fast`, `overrun_window`, or `overrun_snapshot` and does not sleep.
A watchdog thread in compute reads those beats. Silence longer than four
periods, on a stage that has not finished, sets `fault`. The compute process
then exits 3 and the supervisor restarts it. A beat of 0 is startup, not a
stall. `RATEHUB_PACE=0` skips the sleep so the replay test does not depend
on the wall clock. The 5 ms constant is still the integrator step.

`ratehub_bench` sums the same element count 32 times. The contiguous
`int32` array is 2 MiB and stays in a 16 MiB L2 across those passes. The
other array stores one `int32` per 64-byte line, so it occupies 32 MiB and
is re-read from DRAM. The 64-source product table is about 1 KB and is not
used for this measurement. The same program still times two counters on one
cache line against two counters on separate lines. It is not a CI gate.
