# Control Plane — Design

Date: 2026-09-24

## Goal

Add a narrow control plane so an operator can pause ingest and crash compute while the telemetry pipeline stays one-way. The dashboard becomes the writer of two control atomics. No sixth process.

This is also the teaching vehicle for interviews: data plane vs control plane, level vs edge, and why the supervisor cannot be the command consumer while it is blocked in `waitpid`.

## Constraints

- C++20, `-Wall -Wextra -Werror -Wpedantic`.
- Zero new third-party dependencies.
- Hot path must not `malloc`, take mutexes, or open sockets. Ingest and compute only acquire-load or `exchange` control atomics.
- Telemetry records still flow only ingest → ring → compute → snapshot → publish.
- Schema version 4. Mismatch refuses attach.
- `RATEHUB_PACE=0` in tests. Control-plane tests must not depend on wall-clock pacing except where a test explicitly enables it.
- ASan+UBSan and TSan stay separate CI jobs.
- Code comments English. README/DESIGN English.
- Bind HTTP to `127.0.0.1` only.
- No interview-prep documents in the repository root. This spec lives under `docs/superpowers/`.

## Why not reverse the ring

The SPSC ring is a data plane. One producer (ingest frame thread), one consumer (compute 5 ms thread). A reverse write on that ring is a data race.

Backpressure is already implicit: `try_push` returns false when the ring is full, and ingest yields until a slot opens or shutdown is set.

Commands are a different kind of information: latest pause state, and a one-shot crash request. They do not belong on the record ring.

## Why not a sixth process

HTTP already exists on the observer. Supervisor is single-threaded and blocked in `waitpid` for the life of a pipeline child, so it cannot drain a command queue in real time. The processes that must react (ingest, compute) sample the flags themselves.

A dedicated operator process is a later split, only if observer write-vs-read becomes a real isolation need.

## Control block (schema v4)

`ControlPlane` sits in `Layout` after `SupervisorState`, cache-line aligned:

```cpp
struct alignas(64) ControlPlane {
    alignas(64) std::atomic<std::uint32_t> pause_ingest{0};   // 0 run, 1 pause
    alignas(64) std::atomic<std::uint32_t> crash_compute{0};  // 0 idle, 1 pending
};
```

Bump `kSchemaVersion` to `4`.

Two independent flags, two cache lines. They are not packed into one seqlock.

| Flag | Kind | Writer | Reader | Why this primitive |
|---|---|---|---|---|
| `pause_ingest` | Level | observer release-store 0/1 | ingest acquire-load | Stay paused until resume. Latest value is correct. |
| `crash_compute` | Edge | observer release-store 1 | compute `exchange(0, acq_rel)` | Must fire once. A seqlock latest-value sample can miss it. |

`rerun_request` stays on `SupervisorState`. Rerun already works. Do not migrate it in this cut.

`RATEHUB_CRASH` stays for existing supervisor tests. The dashboard crash path is additive.

## Who samples what

**Ingest source thread.** Before `queue.push`, acquire-load `pause_ingest`. If it is 1, skip the push, still store `heartbeat_ingest`, sleep one fast period when pacing is on (or yield when pacing is off), and loop. EOF of the replay file still closes the queue. Pause does not mean forever: it means "do not admit the next frame yet."

**Ingest frame thread.** Unchanged. If the source is paused, the bounded queue drains and then blocks in `pop` until close or the next frame.

**Compute 5 ms thread.** At the start of each cycle, `crash_compute.exchange(0, acq_rel)`. If the old value was non-zero, return from `run_compute` with exit code 2 (same as `RATEHUB_CRASH`). Supervisor already restarts compute and bumps `generation`.

**Publish.** Does not sample the control plane.

**Observer.** `POST /pause` stores `pause_ingest = 1`. `POST /resume` stores `pause_ingest = 0`. `POST /crash-compute` stores `crash_compute = 1`. Existing `POST /reset` is unchanged.

## HTTP

Same POSIX server, still `127.0.0.1`.

| Method | Path | Effect |
|---|---|---|
| POST | `/pause` | `pause_ingest = 1` |
| POST | `/resume` | `pause_ingest = 0` |
| POST | `/crash-compute` | `crash_compute = 1` |
| POST | `/reset` | existing rerun latch |

JSON 2xx body is not required. Empty 204 is enough. Anything else stays 404.

`http_sse_pump` gains an out-parameter (or a small enum) for which POST arrived, so observe can store the matching flag. Do not parse bodies.

## Telemetry JSON

Add two fields, no heap, same `snprintf` path:

- `pause` — `pause_ingest` acquire-load
- `crash_pending` — `crash_compute` acquire-load (may already be 0 if compute consumed it)

Dashboard shows a Pause / Resume toggle and a Crash compute button. After a crash, existing PID/exit/generation fields already tell the story.

## Tests

- Unit: store pause, ingest helper sees it; `exchange` on crash returns 1 then 0.
- HTTP: POST `/pause` then collect_telemetry sees `pause == 1`.
- System: `RATEHUB_PACE=0` supervisor with observe port still exits 0 (no hang).
- System: with observe spawned in-process where possible, posting `/crash-compute` is not required in the unpaced suite if it would race shutdown. Prefer a unit/integration test that maps a `Layout`, stores crash, runs the compute crash check, asserts exit 2.
- Existing `RATEHUB_CRASH=once` test still passes.
- Schema 4: existing schema-mismatch test still refuses an old mapper if one exists; bump the version assert in `test_frame`.

## Documentation

- `docs/DESIGN.md`: control plane section; observer is the control writer; ingest/compute sample flags; telemetry stays one-way.
- `README.md`: dashboard buttons in the live demo blurb.
- Header comments on `ControlPlane`: who may store, who may load, level vs edge.

## Out of scope

- Sixth process.
- Reverse telemetry ring or ACKs on records.
- UDP live ingest.
- Latency histogram.
- Closed-loop plant / setpoints.
- Changing SPSC to MPSC.

Those stay later lessons after this control plane is on GitHub.
