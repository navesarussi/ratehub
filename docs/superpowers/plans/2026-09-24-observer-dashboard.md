# Live Observer Dashboard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a fifth read-only process and a minimal browser dashboard that streams live telemetry from the real POSIX shared-memory mapping via Server-Sent Events (SSE).

**Architecture:** The observer process maps the existing `Layout` read-only, samples control atomics and ring occupancy without touching the hot path, serializes one JSON object per tick, and serves it over HTTP/1.1 SSE from a tiny POSIX-socket server. A single static `web/dashboard.html` (vanilla JS + canvas) subscribes to `/events` and renders process health, ring fill, overruns, and source positions. The supervisor optionally spawns the observer and writes child PIDs into a new `SupervisorState` block in shared memory (schema v3).

**Tech Stack:** C++20, POSIX `shm_open`/`mmap`, POSIX sockets (`socket`/`bind`/`listen`/`accept`), SSE, HTML5 canvas, CMake, existing ASan/TSan CI.

## Global Constraints

- C++20, `-Wall -Wextra -Werror -Wpedantic`.
- Hot path (ingest frame thread, compute fast/window/snapshot threads) must not call `malloc`, must not take mutexes, must not open sockets.
- Observer is read-only: acquire-load atomics, read ring indexes, read triple-buffer snapshot via existing consume pattern; never write to `Layout` except `SupervisorState` is supervisor-only.
- Bump `kSchemaVersion` to `3`; mismatched schema refuses attach (existing tests updated).
- Code comments in English. User-facing README prose in English. No interview-prep documents.
- `RATEHUB_PACE=0` in automated tests; observer tests must not depend on wall-clock pacing.
- ASan+UBSan and TSan remain separate CI jobs; do not combine sanitizers.
- Zero new third-party dependencies (no Boost, no cpp-httplib, no React build chain).
- SSE poll interval default: 100 ms. Configurable via `--interval-ms` on observe subcommand.

---

## File map

| File | Responsibility |
|------|----------------|
| `include/ratehub/spsc_ring.hpp` | Add `occupied()` for observer |
| `include/ratehub/supervisor_state.hpp` | PIDs + last exit codes (supervisor writes) |
| `include/ratehub/layout.hpp` | Add `SupervisorState`; schema v3 layout |
| `include/ratehub/types.hpp` | `kSchemaVersion = 3` |
| `include/ratehub/telemetry.hpp` | `TelemetrySnapshot` struct + `collect_telemetry` + `format_telemetry_json` |
| `src/telemetry.cpp` | Implementation |
| `include/ratehub/http_sse.hpp` | Minimal HTTP server: static file + `/events` SSE |
| `src/http_sse.cpp` | POSIX socket implementation |
| `src/observe.cpp` | `run_observe` main loop |
| `web/dashboard.html` | Browser UI |
| `src/supervisor.cpp` | Write PIDs; optional spawn observe |
| `src/main.cpp` | `observe` subcommand; `run --observe-port` |
| `include/ratehub/roles.hpp` | `run_observe` declaration |
| `tests/test_telemetry.cpp` | Unit tests for JSON + ring fill |
| `tests/test_http_sse.cpp` | Integration: one SSE frame |
| `tests/test_system.cpp` | Extend: run with observe port, fetch one event |
| `docs/DESIGN.md` | Observer process section |
| `README.md` | Demo instructions with browser URL |
| `CMakeLists.txt` | New sources + install `web/` |

---

### Task 1: Ring occupancy API

**Files:**
- Modify: `include/ratehub/spsc_ring.hpp`
- Test: `tests/test_publish.cpp` (extend existing ring tests)

**Interfaces:**
- Produces: `SpscRing::occupied() const noexcept -> std::uint32_t`

- [ ] **Step 1: Write the failing test**

Add to `tests/test_publish.cpp` inside `test_publish()`:

```cpp
ratehub::SpscRing<int, 4> ring2;
expect(ring2.occupied() == 0, "empty ring");
expect(ring2.try_push(1), "push one");
expect(ring2.occupied() == 1, "one item");
expect(ring2.try_push(2) && ring2.try_push(3), "fill ring");
expect(ring2.occupied() == 3, "full ring capacity-1");
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build && ./build/ratehub_tests`
Expected: FAIL — `occupied` not a member of `SpscRing`

- [ ] **Step 3: Implement `occupied()`**

Add to `include/ratehub/spsc_ring.hpp` public section:

```cpp
// Number of records waiting for the consumer. Observer may call this.
// Acquire-loads both indexes. Not for the producer hot path.
std::uint32_t occupied() const noexcept {
    const std::uint32_t write = write_.load(std::memory_order_acquire);
    const std::uint32_t read = read_.load(std::memory_order_acquire);
    return (write - read) & kMask;
}
```

- [ ] **Step 4: Run tests**

Run: `./build/ratehub_tests`
Expected: `ALL PASS`

- [ ] **Step 5: Commit**

```bash
git add include/ratehub/spsc_ring.hpp tests/test_publish.cpp
git commit -m "feat: expose SPSC ring occupancy for observer"
```

---

### Task 2: Schema v3 and supervisor state block

**Files:**
- Create: `include/ratehub/supervisor_state.hpp`
- Modify: `include/ratehub/types.hpp` (`kSchemaVersion = 3`)
- Modify: `include/ratehub/layout.hpp`
- Modify: `tests/test_frame.cpp` (schema constant check)

**Interfaces:**
- Produces: `struct SupervisorState` with atomics `ingest_pid`, `compute_pid`, `publish_pid`, `observe_pid`, `ingest_exit`, `compute_exit`, `publish_exit` (all `std::atomic<std::uint32_t>`, 0 = unknown/not exited).

- [ ] **Step 1: Write the failing test**

In `tests/test_frame.cpp`, change schema expectation to 3 and add:

```cpp
ratehub::Layout layout;
expect(layout.supervisor.ingest_pid.load(std::memory_order_relaxed) == 0u, "supervisor block zeroed");
```

Add `#include "ratehub/layout.hpp"`.

- [ ] **Step 2: Run test — expect compile fail** (Layout has no `supervisor` yet)

- [ ] **Step 3: Create `include/ratehub/supervisor_state.hpp`**

```cpp
#pragma once
// Written only by the supervisor process. Observer acquire-loads for display.
#include <atomic>
#include <cstdint>

namespace ratehub {

struct alignas(64) SupervisorState {
    std::atomic<std::uint32_t> ingest_pid{0};
    std::atomic<std::uint32_t> compute_pid{0};
    std::atomic<std::uint32_t> publish_pid{0};
    std::atomic<std::uint32_t> observe_pid{0};
    std::atomic<std::uint32_t> ingest_exit{0};
    std::atomic<std::uint32_t> compute_exit{0};
    std::atomic<std::uint32_t> publish_exit{0};
};

}  // namespace ratehub
```

- [ ] **Step 4: Update `types.hpp`**

```cpp
constexpr std::uint16_t kSchemaVersion = 3;
// comment: v3 adds SupervisorState to Layout
```

- [ ] **Step 5: Update `layout.hpp`**

```cpp
#include "ratehub/supervisor_state.hpp"
// inside Layout:
alignas(64) SupervisorState supervisor;
```

Place `supervisor` immediately after `control`.

- [ ] **Step 6: Run tests**

Run: `cmake --build build && ./build/ratehub_tests`
Expected: `ALL PASS` (system test still uses schema match on create)

- [ ] **Step 7: Commit**

```bash
git add include/ratehub/supervisor_state.hpp include/ratehub/types.hpp include/ratehub/layout.hpp tests/test_frame.cpp
git commit -m "feat: schema v3 adds supervisor PID block to shared layout"
```

---

### Task 3: Supervisor writes PIDs and exit codes

**Files:**
- Modify: `src/supervisor.cpp`
- Modify: `tests/test_system.cpp` (optional: not required if PIDs nonzero hard to assert cross-process; skip PID value asserts)

**Interfaces:**
- Consumes: `Layout::supervisor`
- Produces: PIDs stored with `memory_order_release` after each `posix_spawn`; exit codes stored after `waitpid`

- [ ] **Step 1: Add helper in `supervisor.cpp`**

```cpp
void store_pid(std::atomic<std::uint32_t>& slot, pid_t pid) {
    slot.store(static_cast<std::uint32_t>(pid), std::memory_order_release);
}
void store_exit(std::atomic<std::uint32_t>& slot, int code) {
    slot.store(static_cast<std::uint32_t>(code), std::memory_order_release);
}
```

- [ ] **Step 2: After each spawn, store PID**

```cpp
const pid_t ingest = spawn_child(...);
store_pid(mapping.layout->supervisor.ingest_pid, ingest);
// same for publish, compute inside restart loop
```

- [ ] **Step 3: After each `wait_child`, store exit**

```cpp
const int ingest_status = wait_child(ingest);
store_exit(mapping.layout->supervisor.ingest_exit, ingest_status);
```

- [ ] **Step 4: Run existing system tests**

Run: `RATEHUB_PACE=0 ./build/ratehub_tests`
Expected: `ALL PASS`

- [ ] **Step 5: Commit**

```bash
git add src/supervisor.cpp
git commit -m "feat: supervisor publishes child PIDs and exit codes to shm"
```

---

### Task 4: Telemetry collection and JSON formatting

**Files:**
- Create: `include/ratehub/telemetry.hpp`
- Create: `src/telemetry.cpp`
- Create: `tests/test_telemetry.cpp`
- Modify: `tests/main.cpp`, `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `struct TelemetrySnapshot { std::uint64_t ts_ns; std::uint32_t generation, shutdown, fault, ring_occupied, ring_capacity, overrun_fast, overrun_window, overrun_snapshot; std::uint64_t hb_ingest, hb_compute, hb_publish, hb_fast, hb_window, hb_snapshot; std::uint32_t ingest_pid, compute_pid, publish_pid; struct SourcePoint { std::uint32_t id; std::int32_t x, y; std::uint32_t publishes; }; static constexpr std::size_t kMaxPoints = 8; SourcePoint sources[kMaxPoints]; std::size_t source_count; };`
  - `TelemetrySnapshot collect_telemetry(const Layout& layout, std::uint64_t now_ns) noexcept;`
  - `std::size_t format_telemetry_json(const TelemetrySnapshot& snap, char* out, std::size_t cap) noexcept;` — returns bytes written excluding NUL, or 0 if truncated.

- [ ] **Step 1: Write failing test `tests/test_telemetry.cpp`**

```cpp
#include "ratehub/telemetry.hpp"
#include "ratehub/layout.hpp"
#include <cstdio>
#include <cstring>

int test_telemetry() {
    ratehub::Layout layout;
    layout.control.generation.store(2, std::memory_order_relaxed);
    layout.ring.try_push(ratehub::Record{1, 1, 0, 100, 0, 10, 0});
    const auto snap = ratehub::collect_telemetry(layout, 999);
    if (snap.generation != 2 || snap.ring_occupied != 1) {
        std::fprintf(stderr, "FAIL collect\n");
        return 1;
    }
    char buf[512];
    const std::size_t n = ratehub::format_telemetry_json(snap, buf, sizeof(buf));
    if (n == 0 || std::strstr(buf, "\"generation\":2") == nullptr) {
        std::fprintf(stderr, "FAIL json %s\n", buf);
        return 1;
    }
    return 0;
}
```

Wire into `tests/main.cpp`.

- [ ] **Step 2: Run — expect link/compile fail**

- [ ] **Step 3: Implement `collect_telemetry`**

Read all control atomics with `memory_order_acquire`. Set `ring_occupied = layout.ring.occupied()`, `ring_capacity = kRingCapacity - 1`. Loop `id` 0..63, `layout.window[id].load(row)`, if `row.publishes > 0` append to `sources` until `kMaxPoints`. Use steady clock for `ts_ns` parameter passed in.

- [ ] **Step 4: Implement `format_telemetry_json`**

Hand-written `snprintf` chain — no heap. Example output:

```json
{"ts_ns":999,"generation":2,"shutdown":0,"fault":0,"ring":{"occupied":1,"capacity":1023},"overrun":{"fast":0,"window":0,"snapshot":0},"hb":{"ingest":0,"compute":0,"publish":0,"fast":0,"window":0,"snapshot":0},"pids":{"ingest":0,"compute":0,"publish":0},"sources":[{"id":1,"x":100,"y":0,"p":1}]}
```

- [ ] **Step 5: Add to `CMakeLists.txt` library sources**

```cmake
src/telemetry.cpp
```

- [ ] **Step 6: Run tests**

Expected: `ALL PASS`

- [ ] **Step 7: Commit**

```bash
git add include/ratehub/telemetry.hpp src/telemetry.cpp tests/test_telemetry.cpp tests/main.cpp CMakeLists.txt
git commit -m "feat: telemetry snapshot collection and JSON formatting"
```

---

### Task 5: Minimal HTTP + SSE server

**Files:**
- Create: `include/ratehub/http_sse.hpp`
- Create: `src/http_sse.cpp`
- Create: `tests/test_http_sse.cpp`

**Interfaces:**
- Produces:
  - `struct HttpSseServer { int listen_fd = -1; };`
  - `bool http_sse_listen(HttpSseServer& srv, std::uint16_t port) noexcept;`
  - `void http_sse_close(HttpSseServer& srv) noexcept;`
  - `// Blocks until one request handled. Returns true on SSE client connected.`
  - `bool http_sse_serve_once(HttpSseServer& srv, const char* web_root, char* sse_payload, std::size_t sse_len) noexcept;`
  - Routes: `GET /` or `/dashboard.html` → serve `web/dashboard.html`; `GET /events` → `text/event-stream` with one `data: ...\n\n` per call.

- [ ] **Step 1: Write failing integration test**

`tests/test_http_sse.cpp` spawns server on port 0 (or fixed 19080), connects with `socket`/`connect` to `127.0.0.1`, sends:

```
GET /events HTTP/1.1\r\nHost: localhost\r\n\r\n
```

Reads until `\n\n`, expects substring `data: {`.

Use `fork` + child runs one `http_sse_serve_once` with payload `{"ok":1}`, parent connects. Skip if port busy.

- [ ] **Step 2: Implement POSIX server**

`http_sse.cpp`:
- `socket(AF_INET, SOCK_STREAM, 0)`
- `setsockopt(SO_REUSEADDR)`
- `bind` port, `listen(1)`
- `accept` one client
- Parse first line; if `/events`, write headers:

```
HTTP/1.1 200 OK\r\n
Content-Type: text/event-stream\r\n
Cache-Control: no-cache\r\n
Connection: keep-alive\r\n
\r\n
```

Then `snprintf(buf, "data: %s\n\n", sse_payload)` and `send`.

For `/` serve file `web_root/dashboard.html` with `Content-Type: text/html`.

- [ ] **Step 3: Run test**

Expected: PASS

- [ ] **Step 4: Commit**

```bash
git add include/ratehub/http_sse.hpp src/http_sse.cpp tests/test_http_sse.cpp tests/main.cpp CMakeLists.txt
git commit -m "feat: minimal POSIX HTTP server with SSE endpoint"
```

---

### Task 6: Observer process loop

**Files:**
- Create: `src/observe.cpp`
- Modify: `include/ratehub/roles.hpp`
- Modify: `src/main.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `int run_observe(const char* shm_name, std::uint16_t port, std::uint32_t interval_ms) noexcept;`

- [ ] **Step 1: Implement `run_observe`**

```cpp
int run_observe(const char* shm_name, std::uint16_t port, std::uint32_t interval_ms) {
    ShmMapping mapping;
    if (shm_open_existing(shm_name, mapping) != ShmStatus::Ok) return 1;
    HttpSseServer srv;
    if (!http_sse_listen(srv, port)) return 1;
    const char* web = "web";  // relative to cwd when run from repo root
    char json[2048];
    while (mapping.layout->control.shutdown.load(std::memory_order_acquire) == 0) {
        const auto snap = collect_telemetry(*mapping.layout, now_ns());
        const std::size_t n = format_telemetry_json(snap, json, sizeof(json));
        if (n == 0) continue;
        if (!http_sse_serve_once(srv, web, json, n)) break;
        if (pacing_enabled()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
        } else {
            std::this_thread::yield();
        }
    }
    http_sse_close(srv);
    shm_close(mapping);
    return 0;
}
```

Note: For production UX, refactor Task 5 to **keep SSE connection open** and push multiple events on one socket (loop `send` on same fd). Adjust `http_sse_serve_once` → `http_sse_handle_client(int client_fd, ...)` called from accept loop. Plan requires persistent SSE in Task 6 revision:

- Accept client once at start.
- If path `/events`, enter loop: collect → `data: ...\n\n` → sleep(interval) until shutdown.
- If path `/`, send HTML and close.

- [ ] **Step 2: Add CLI**

`src/main.cpp`:

```cpp
if (argc >= 4 && std::strcmp(argv[1], "observe") == 0) {
    const std::uint16_t port = static_cast<std::uint16_t>(std::atoi(argv[3]));
    const std::uint32_t interval = argc >= 5 ? static_cast<std::uint32_t>(std::atoi(argv[4])) : 100u;
    return ratehub::run_observe(argv[2], port, interval);
}
```

Usage: `ratehub observe <shm> <port> [interval_ms]`

- [ ] **Step 3: Manual smoke test**

```bash
./build/make_replay /tmp/r.bin
./build/ratehub run /tmp/r.bin /tmp/out.txt &
# note shm name from supervisor — for manual test, temporarily log shm name in supervisor
./build/ratehub observe /rh-XXXX 8080 &
curl -N http://127.0.0.1:8080/events | head -3
```

- [ ] **Step 4: Commit**

```bash
git add src/observe.cpp include/ratehub/roles.hpp src/main.cpp CMakeLists.txt
git commit -m "feat: observe process streams live telemetry over SSE"
```

---

### Task 7: Browser dashboard

**Files:**
- Create: `web/dashboard.html`

**Interfaces:**
- Consumes: SSE JSON shape from `format_telemetry_json`

- [ ] **Step 1: Create `web/dashboard.html`**

Single file, no build step. Sections:

1. **Pipeline diagram** — four boxes (ingest, ring, compute, publish); CSS class `ok` / `stale` based on heartbeat age vs threshold (client compares successive payloads).
2. **Meters** — ring `occupied/capacity` as horizontal bar.
3. **Counters** — generation, overruns, fault, PIDs.
4. **Canvas** — scatter plot: for each `sources[]`, map `x`/`y` mm to pixels (scale auto).

JS:

```javascript
const es = new EventSource('/events');
es.onmessage = (e) => {
  const snap = JSON.parse(e.data);
  updateUI(snap);
};
```

Dark theme, monospace counters, no frameworks.

- [ ] **Step 2: Verify in browser**

Run observe + open `http://127.0.0.1:8080/`

- [ ] **Step 3: Commit**

```bash
git add web/dashboard.html
git commit -m "feat: add live telemetry dashboard page"
```

---

### Task 8: Supervisor spawns observer (`run --observe-port`)

**Files:**
- Modify: `src/supervisor.cpp`
- Modify: `src/main.cpp`
- Modify: `include/ratehub/roles.hpp`
- Modify: `tests/test_system.cpp`

**Interfaces:**
- Produces: `int run_supervisor(const char* exe, const char* replay_path, const char* out_path, std::uint16_t observe_port);` — port `0` means no observer.

- [ ] **Step 1: Extend `run` CLI**

```cpp
// ratehub run <replay> <out> [--observe-port N]
```

Parse optional `--observe-port 8080` from argv.

- [ ] **Step 2: Spawn observer after mapping created**

```cpp
pid_t observe = -1;
if (observe_port != 0) {
    char port_buf[16];
    std::snprintf(port_buf, sizeof(port_buf), "%u", observe_port);
    observe = spawn_child(exe, "observe", name.c_str(), port_buf);
    store_pid(mapping.layout->supervisor.observe_pid, observe);
}
```

Extend `spawn_child` to pass 4th arg for observe (port string). Update `observe` subcommand to accept port as argv[3].

- [ ] **Step 3: Wait observer on shutdown**

Before `shm_close`, `wait_child(observe)` if spawned.

- [ ] **Step 4: System test — fetch SSE during run**

In `test_system.cpp`, optional test with `--observe-port` and a background thread that `connect`s to localhost, reads one `data:` line, asserts `"generation"` present. Use port `18080 + (pid % 1000)` to avoid collisions.

- [ ] **Step 5: Run full suite**

`./build/ratehub_tests` and TSan build.

- [ ] **Step 6: Commit**

```bash
git add src/supervisor.cpp src/main.cpp include/ratehub/roles.hpp tests/test_system.cpp
git commit -m "feat: supervisor optionally spawns live observer"
```

---

### Task 9: Documentation and README demo

**Files:**
- Modify: `docs/DESIGN.md`
- Modify: `README.md`

- [ ] **Step 1: Add Observer section to DESIGN.md**

Document fifth process, read-only contract, SSE endpoint, schema v3 supervisor block, explicit non-goals (no WebSocket, no React, no hot-path HTTP).

- [ ] **Step 2: Update README quick start**

```bash
./build/ratehub run replay.bin snapshot.txt --observe-port 8080
# open http://127.0.0.1:8080/
```

- [ ] **Step 3: Commit**

```bash
git add docs/DESIGN.md README.md
git commit -m "docs: document live observer dashboard"
```

---

## Spec self-review

| Requirement | Task |
|-------------|------|
| Fifth read-only process | Task 6, 8 |
| Live browser visualization | Task 7 |
| No hot-path sockets/mutex | Global Constraints + Task 6 separate process |
| Real shm data, not fake animation | Task 4 reads `Layout` |
| Schema versioning | Task 2 |
| Tests + CI | Tasks 1–5, 8 |
| No interview docs | Global Constraints |
| Low-level credibility (POSIX sockets) | Task 5 |

**Placeholder scan:** No TBD steps. All interfaces named.

**Type consistency:** `TelemetrySnapshot`, `SupervisorState`, `occupied()`, `run_observe` aligned across tasks.

---

## Out of scope (YAGNI for this plan)

- Event ring / binary trace log (future `ratehub trace`)
- GitHub Pages hosting (local demo is enough; add later)
- WebSocket, React, Docker-only demo
- Renaming "window" thread to true 25 ms history buffer
- Binding HTTP on `0.0.0.0`

---

## Addendum (richer demo, 2026-09-24)

Spec: `docs/superpowers/specs/2026-09-24-observer-dashboard-design.md`.

Deltas vs tasks 1–9:

- `SupervisorState` also has `observe_release`. After pipeline, supervisor sets it to 1 immediately when `RATEHUB_PACE=0`, otherwise waits for SIGINT/SIGTERM.
- Telemetry JSON includes `idle`, `done`, `exits`, `observe` pid, and source `seq`. Up to 16 sources. Buffer 4096.
- HTTP binds `127.0.0.1`. Persistent SSE. Web root is `dirname(argv[0])/web`.
- Dashboard adds sparklines, source table, trails, idle/fault badges.
- Ingest sleeps `kFastPeriodNs` after each queued frame when pacing is on.
- `make_replay <path> --demo` writes 8 sources × 500 frames.

### Task 10: Ingest pacing

**Files:** `src/ingest.cpp`

After a successful `queue.push(frame)` in the source thread, if `pacing_enabled()`, sleep `kFastPeriodNs`. Tests already set `RATEHUB_PACE=0`.

### Task 11: Demo replay

**Files:** `tools/make_replay.cpp`

`make_replay [path]` unchanged (2 frames). `make_replay [path] --demo` writes sources 0..7, 500 frames each, interleaved, distinct pos/vel.

### Task 12: Idle hold

**Files:** `src/supervisor.cpp`, `src/observe.cpp`

Observer loops until `observe_release != 0` or SIGINT. Supervisor after waiting ingest/compute/publish: if observe spawned and `pacing_enabled()`, wait for SIGINT, then store `observe_release = 1` and `wait_child(observe)`.

---

## Execution

Inline TDD in this session. Do not commit unless the user asks.
