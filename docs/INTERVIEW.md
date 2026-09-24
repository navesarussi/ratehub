# Interview map

Where `subject_list.md` concepts appear in this repository. Use this to open
the right file in a screen share.

| Topic | Where |
|-------|--------|
| Big-endian wire format, CRC, no struct overlay | `include/ratehub/frame.hpp`, `src/frame.cpp` |
| Strict aliasing / `memcpy` decode | `decode_frame` copies to a local buffer first |
| `int64` accumulator, range check before `int32` store | `src/fleet.cpp` |
| Structure-of-arrays hot table | `include/ratehub/fleet.hpp` |
| SPSC ring, release/acquire on indexes | `include/ratehub/spsc_ring.hpp` |
| Seqlock, relaxed payload atomics | `include/ratehub/seqlock.hpp` |
| Triple buffer, writer/reader never share a slot | `include/ratehub/triple_buffer.hpp` |
| False sharing, cache-line alignment | `include/ratehub/shm_control.hpp`, `src/bench.cpp` |
| `mmap` / POSIX shared memory | `src/shm_region.cpp` |
| Multi-process, supervisor restart, generation | `src/supervisor.cpp`, `tests/test_system.cpp` |
| Mutex queue on cold path only | `include/ratehub/bounded_queue.hpp` |
| Soft deadlines, overrun counters | `include/ratehub/pace.hpp`, `src/compute.cpp` |
| Watchdog on heartbeat silence | `src/compute.cpp` watchdog thread |

## 30-second pitch

> Personal C++ systems project. Four processes share one POSIX mapping: ingest
> decodes big-endian frames into a lock-free SPSC ring; compute integrates in
> a structure-of-arrays table and publishes through seqlocks and a triple buffer;
> publish hands a finished snapshot to readers. Production background is C on
> hard real-time avionics — the publication pattern here is compute-then-single-
> publish, the same class as fixing a clear-then-store bug, not an atomic read.
> Linux deadlines are soft: overruns are counted, a watchdog restarts a stuck
> compute process.

## What this is not

Not MIL-STD-1553, not an RTOS port, not a kernel module, not distributed
storage. Stated explicitly so the interview stays honest.
