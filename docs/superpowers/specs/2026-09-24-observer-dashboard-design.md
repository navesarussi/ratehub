# Live Observer Dashboard — Design

Date: 2026-09-24

## Goal

Add a fifth, read-only process that maps the existing shared `Layout` and serves a local browser dashboard. The page shows live pipeline health, ring occupancy, overruns, heartbeats, PIDs, and source positions from real shared-memory samples. A paced multi-source replay keeps the view alive for about 20 seconds; after the pipeline finishes the page stays on the last snapshot with an idle banner until Ctrl+C.

## Constraints

- C++20, `-Wall -Wextra -Werror -Wpedantic`.
- Zero new third-party dependencies.
- Hot path (ingest frame thread, compute rate threads) must not `malloc`, take mutexes, or open sockets.
- Observer never writes `Layout` except by reading atomics. `SupervisorState` is supervisor-only.
- Schema version 3. Mismatch refuses attach.
- `RATEHUB_PACE=0` in tests. Observer tests must not depend on wall-clock pacing.
- ASan+UBSan and TSan stay separate CI jobs.
- Code comments English. README/DESIGN English.
- Bind HTTP to `127.0.0.1` only.

## Process model

Five processes. Supervisor is still single-threaded.

| Process | Role |
|---|---|
| Supervisor | Create mapping, spawn children, publish PIDs/exits, optional observe hold |
| Ingest | Unchanged except: when pacing is on, sleep `kFastPeriodNs` after each queued frame |
| Compute | Unchanged. Live positions come from the 25 ms window seqlocks |
| Publish | Unchanged |
| Observe | Map read-mostly, collect telemetry, HTTP + SSE |

The triple-buffer snapshot is still published once at the end of compute. The dashboard therefore samples `window[]`, not `snapshots`.

## Shared layout (schema v3)

`SupervisorState` sits immediately after `ShmControl`, cache-line aligned:

- `ingest_pid`, `compute_pid`, `publish_pid`, `observe_pid`
- `ingest_exit`, `compute_exit`, `publish_exit` (0 = unknown / not exited)
- `observe_release` (0 = observer runs, 1 = observer must exit)

Supervisor release-stores PIDs after `posix_spawn` and exit codes after `waitpid`. After the pipeline children have exited, supervisor store-releases `observe_release = 1` unless holding.

## Idle hold

When `run --observe-port N` and pacing is enabled, supervisor waits for SIGINT/SIGTERM after the pipeline before setting `observe_release`. Observer ignores neither mapping validity nor `observe_release`; it keeps serving the last JSON with `"idle":1`. Tests set `RATEHUB_PACE=0`, so hold is skipped and the run still terminates.

Observer also exits on SIGINT/SIGTERM so a manual `ratehub observe` can stop.

## Telemetry JSON

One object per SSE event. No heap. Hand-written `snprintf`. Fields:

- `ts_ns`, `generation`, `shutdown`, `fault`, `idle`
- `ring.occupied`, `ring.capacity` (`kRingCapacity - 1`)
- `overrun.{fast,window,snapshot}`
- `done.{ingest,fast,window,snapshot,compute}`
- `hb.{ingest,compute,publish,fast,window,snapshot}`
- `pids.{ingest,compute,publish,observe}`
- `exits.{ingest,compute,publish}`
- `sources[]` up to 16 entries: `id`, `x`, `y`, `p` (publishes), `seq` from window seqlocks where `publishes > 0`

`idle` is 1 when `shutdown != 0` and `ingest_done != 0` and `compute_done != 0`.

## HTTP

Minimal POSIX sockets on `127.0.0.1`.

- `GET /` and `GET /dashboard.html` → `web/dashboard.html`
- `GET /events` → `text/event-stream`, persistent `data: <json>\n\n`
- Anything else → 404

Web root: directory of `argv[0]` plus `/web`, else `web`. CMake copies `web/` into the build directory.

Default SSE interval 100 ms, overridable as `ratehub observe <shm> <port> [interval_ms]`.

## Dashboard (single static file)

Vanilla JS, no build, dark operations theme.

1. Header: live / idle / fault badge, generation, schema.
2. Pipeline row: ingest, ring, compute (fast / window / snapshot), publish, observe. Color from heartbeat change vs done/fault.
3. Ring fill bar (`occupied/capacity`).
4. Counters: overruns, PIDs, exits, done flags.
5. Heartbeat sparklines — client keeps the last 80 samples.
6. Source table.
7. Canvas scatter of `sources[]` with short trails.

Client infers stale when a heartbeat is unchanged across ~20 SSE ticks and the matching `done` flag is 0.

## Demo replay

`make_replay` default stays two frames (tests). `make_replay <path> --demo` writes 8 sources × 500 frames (4000 frames, ~20 s wall-clock with ingest pacing). Distinct starting positions and velocities.

## Error handling

- Bind/listen failure: observer exits 1. Supervisor treats a non-zero observe exit as failure after wait.
- Schema mismatch: observer exits 1 (existing attach path).
- SSE client disconnect: drop the fd, accept again.
- JSON truncation: skip that tick (return 0 from formatter).
- Path traversal on the static file: reject any path other than `/` and `/dashboard.html`.

## Testing

- Unit: `occupied()`, schema 3 + zeroed supervisor block, telemetry collect + JSON keys.
- HTTP: listen on port 0, one client, one `data: {` frame (threads, not fork).
- System: existing replay assertions unchanged. Optional `--observe-port` run with `RATEHUB_PACE=0` still returns 0.
- Ingest pacing is not a wall-clock test; `RATEHUB_PACE=0` keeps the two-frame supervisor test fast.

## Non-goals

- WebSocket, React, Docker, GitHub Pages.
- Event ring / binary trace log.
- Changing the 100 ms thread to publish intermediate snapshots.
- Binding on `0.0.0.0`.
