# ABox — Phase-by-Phase Acceptance Test Plan

**Project:** ABox, C++20 Linux sandbox runtime  
**Target environment:** Ubuntu/WSL2 for development; disposable Ubuntu VM for privileged or destructive testing  
**Status:** Test specifications for features in progress; commands for later phases are **proposed CLI contracts**, not claims that the commands already work.

## Rules for every phase

1. Build with `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j` and run `ctest --test-dir build --output-on-failure`. Use AddressSanitizer/UBSan for applicable unprivileged unit tests, but don't assume instrumented binaries behave identically in all namespace or privilege scenarios.
2. Run unit tests without privileges. Privileged integration tests must use a dedicated, explicitly delegated cgroup subtree, and run in a disposable WSL test distribution or VM. Never modify systemd-managed cgroups directly, mount or unmount host filesystems, or run untrusted workloads as root.
3. Name tests by phase (`cli_*`, `process_*`, `cgroup_*`, `ns_*`, `daemon_*`, `storage_*`, `monitor_*`, `tui_*`, `security_*`). Mark privileged tests separately, e.g. CTest label `requires_privileges`; skip with a reason if a prerequisite is unavailable, don't silently pass.
4. Collect stdout, stderr, exit code, duration, relevant cgroup counters, PIDs, cgroup membership, namespace links, and pre/post resource snapshots for failures.
5. Cleanup must run after every test, including failed tests; no leftover cgroups, mounts, loop devices, Unix sockets, or workload descendants. Tests must use unique names and temporary directories.
6. Each phase must pass existing regression tests. Do not treat a built binary or a manually eyeballed log as proof of behavior.

## Stage 0–2 — Existing CLI, processes, cgroups, stats (regression gate)

| ID | Test | Action | Pass criteria |
|---|---|---|---|
| CLI-01 | Help | `abox --help` | Exit 0; usage and supported commands shown |
| CLI-02 | Version | `abox --version` | Exit 0; application version, not compiler version |
| CLI-03 | Invalid command | `abox nonsense` | Nonzero exit; actionable error on stderr |
| CLI-04 | Missing input | `abox run --` | Nonzero exit; no process created |
| CLI-05 | Bad option | `abox run --memory nonsense -- /bin/true` | Nonzero exit; no cgroup leaked |
| PROC-01 | Arguments | `abox run -- /bin/echo hello world` | Exact arguments forwarded; exit 0 |
| PROC-02 | Exit code | `abox run -- /bin/sh -c 'exit 42'` | ABox exits 42 |
| PROC-03 | Exec failure | `abox run -- /definitely/nonexistent` | Nonzero exit; no leaked child or cgroup |
| PROC-04 | Signal status | `abox run -- /bin/sh -c 'kill -TERM $$'` | Signal reported; conventional status 143, if that's the documented policy |
| PROC-05 | Wait/reap | Repeated short-lived children | No zombie children after completion |
| CG-01 | CPU limit | CPU-bound fixture for ~4–6s with `--cpus 0.5` | Effective `cpu.max` matches requested quota; `cpu.stat` shows usage within expected quota over long interval; throttling observable when CPU-saturated (allow scheduling variance) |
| CG-02 | Memory limit | Memory-touch fixture gradually allocates above configured limit | `memory.max` matches; workload OOM-killed or fails safely; memory event counters observed; no host instability |
| CG-03 | Before-exec placement | Probe fixture reads `/proc/self/cgroup` at process start | Running workload is already in ABox workload cgroup; never launches unrestricted on attachment failure |
| CG-04 | Missing controllers | Run without an available or delegated controller | Clear error; no partial cgroup or unintended workload execution |
| CG-05 | Cleanup | Success, nonzero exit, exec failure, interrupted wait | No ABox workload cgroup left after all tracked descendants are terminated |
| STATS-01 | Accounting | Compare reported `usage_usec`, peak memory and event counters to cgroup files | Same source values, correct units and field names |
| STATS-02 | Missing metric | Fixture missing `memory.peak` or throttling fields | Displays unavailable, not false zero; no crash |
| STATS-03 | Multi-core CPU | Parallel compute fixture | CPU-equivalent usage can exceed 1 when quota permits; no hard-coded 100% ceiling |

**Execution note:** Current ABox's temporary `/sys/fs/cgroup/abox` test setup may need elevated privileges. Move integration tests to systemd delegation before making them routine. `memory.max` limits memory charges; it doesn't reserve memory. `cpu.max` limits CPU-time quota; it doesn't pin particular cores.

## Phase 3 — Namespace isolation

### UTS

| ID | Scenario | Pass criteria |
|---|---|---|
| NS-UTS-01 | `abox run --uts -- /bin/hostname` | Workload prints sandbox hostname; normal run prints host hostname |
| NS-UTS-02 | Compare host hostname before/after | Host hostname unchanged |
| NS-UTS-03 | Compare `/proc/self/ns/uts` link inside workload vs host | Namespace inode differs with UTS isolation enabled |
| NS-UTS-04 | Namespace creation fails (permission denied) | No fallback to unisolated execution; error and cleanup |

### PID + init + /proc

| ID | Scenario | Pass criteria |
|---|---|---|
| NS-PID-01 | Run fixture as namespace init | `getpid()` returns 1 *inside* its PID namespace |
| NS-PID-02 | Run one child and inspect PIDs | Child has sandbox PID and separate host PID; namespace PID view is consistent |
| NS-PID-03 | Run `ps` inside isolated `/proc` | Lists sandbox-visible processes; excludes host-only processes |
| NS-PID-04 | Spawn child that exits without parent waiting | `abox-init` reaps orphaned children; no persistent zombies |
| NS-PID-05 | Terminate sandbox init | Remaining sandbox processes terminate; host unaffected |
| NS-PID-06 | Deliver SIGTERM to sandbox | Supervisor forwards/shuts down per documented policy and reaps all children |

A fresh `/proc` should be mounted by a task in the target PID namespace, within the sandbox mount namespace, or `ps` can show the wrong PID view. PID 1 receives special signal treatment.

### Mount and rootfs

| ID | Scenario | Pass criteria |
|---|---|---|
| NS-MNT-01 | Bind/mount within sandbox | New mount not visible in host's mount table |
| NS-MNT-02 | Compare `/proc/self/ns/mnt` identifiers | Different identifiers for isolated workload and host |
| NS-MNT-03 | Put a unique sentinel file in host-only directory; attempt to open same absolute path from sandbox | File not accessible through isolated rootfs |
| NS-MNT-04 | Try `..` traversal and `/proc/self/root` access to reach host-only sentinel | Cannot escape isolated rootfs via these paths; record any unsupported hardening explicitly |
| NS-MNT-05 | Exit workload; inspect host mount table | No propagated mounts or leaked sandbox mounts |
| NS-MNT-06 | Invalid rootfs or failed pivot_root | No fallback to host rootfs; fail closed and clean up |

### User and IPC

| ID | Scenario | Pass criteria |
|---|---|---|
| NS-USER-01 | Read `/proc/self/uid_map`, `gid_map` | Mappings match requested plan; no unintended host-root mapping |
| NS-USER-02 | Create file inside sandbox as mapped UID | Ownership on host corresponds to mappings; cannot modify host-only sentinel |
| NS-USER-03 | Unprivileged user requests forbidden operation | Appropriate permission failure; no host mutation |
| NS-IPC-01 | Create System V IPC identifier in sandbox | Identifier/object not visible from isolated IPC namespace of another sandbox |

**Phase gate:** CPU/memory controls continue to apply after namespace creation. Tests use a prepared disposable rootfs with necessary binaries and `/proc` mountpoint.

## Phase 4 — Persistent daemon and lifecycle

**Proposed CLI:** `abox create <name>`, `abox start <name>`, `abox exec <name> -- <program>`, `abox list`, `abox stop <name>`, `abox delete <name>`.

| ID | Scenario | Pass criteria |
|---|---|---|
| D-01 | Create `testbox` with known config | Configuration persisted; listed as Created; no running workload required |
| D-02 | Start then list | State changes to Running; namespace init and cgroup exist |
| D-03 | Start already-running box | Idempotent success or documented error; no duplicate init |
| D-04 | Run `abox exec testbox -- /bin/echo hello` | Command executes inside existing namespaces/rootfs/cgroup |
| D-05 | Start two concurrent exec sessions | Both run; independent exit codes; no mix-up of stdio/PIDs |
| D-06 | Disconnect CLI while sandbox continues | Workload keeps running; reconnection works while WSL/VM is alive |
| D-07 | Restart daemon without stopping sandbox | Existing sandbox discovered/reconciled; does not claim false state or duplicate it |
| D-08 | `abox stop testbox` | Graceful stop within deadline; forced stop if necessary; no descendants left |
| D-09 | Stop twice | Deterministic and documented result, no crash |
| D-10 | Delete running sandbox | Rejected or explicitly stopped as documented; never silent data loss |
| D-11 | Delete stopped sandbox | Config and associated resources removed; not present in list |
| D-12 | Create duplicate name, malformed name, nonexistent target | Correct errors, no mutation |
| D-13 | Kill/restart daemon during create/start/stop | State reconciliation; no unrecoverable partial state or zombie processes |
| D-14 | Send malformed/truncated IPC frame | Error response or closed connection; daemon survives |
| D-15 | Two simultaneous `start` requests | Exactly one sandbox instance created |

**WSL nuance:** Closing a CLI/terminal is different from shutting down WSL. Persistent filesystem/config must survive WSL restarts; in-memory processes do not. Recovery should re-report stopped state or explicitly re-start from configuration according to the documented policy.

## Phase 5 — Persistent storage and capacity

Use a disposable dedicated storage backend (for example a 64 MiB ext4 loopback image) with test files only. Do not use host root or personal directories.

| ID | Scenario | Pass criteria |
|---|---|---|
| FS-01 | Create sandbox with `--storage 64M` | Filesystem reports correct configured logical capacity within format overhead |
| FS-02 | Write `hello.txt`, stop, start, read | Exact content persists |
| FS-03 | Create two sandboxes; write in A | File does not appear in B unless explicitly shared |
| FS-04 | Write data until capacity exceeded | `ENOSPC` or `EDQUOT` is returned; host not accidentally filled; sandbox does not corrupt |
| FS-05 | Stop and remove sandbox | No leaked mounts, loop devices, temp files, or open handles |
| FS-06 | Mount fails halfway | Rollback successful; host mount table unchanged |
| FS-07 | Attempt path traversal through sandbox root | Cannot access host-only sentinel outside expected mounts |
| FS-08 | Restart daemon/WSL, start sandbox | Persistent data available after clean restoration; active processes not assumed to survive WSL shutdown |
| FS-09 | Two concurrent sandboxes using same base image | Writes remain isolated; shared base stays unchanged if using overlay/COW |

## Phase 6 — Live metrics engine

| ID | Scenario | Pass criteria |
|---|---|---|
| MON-01 | Idle workload | CPU rate near 0; memory counters reasonable; no division-by-zero |
| MON-02 | CPU-bound workload | CPU rate computed from `usage_usec` deltas / elapsed wall time; correct CPU-equivalent units |
| MON-03 | Memory allocation then release | `memory.current` rises/falls; `memory.peak` stays at or above observed current while cgroup exists |
| MON-04 | CPU throttling | `nr_throttled` and `throttled_usec` match cgroup counters |
| MON-05 | OOM test | `memory.events` OOM/kill values reflected accurately; program crash doesn't crash monitor |
| MON-06 | Two workloads simultaneously | Per-sandbox metrics remain separated; aggregate not double counted |
| MON-07 | Rapid start/exit | No stale PID data or crashes from missing `/proc/<pid>` |
| MON-08 | Counter disappears / permissions fail | Unavailable metric labeled; no fabricated zeros |
| MON-09 | Sample intervals vary | Rates use actual monotonic time delta; avoid fixed-refresh assumptions |
| MON-10 | Monitor overhead benchmark | Record CPU use and workload throughput with/without polling; report measured overhead and hardware |

## Phase 7 — TUI acceptance tests

Prefer automated pseudo-terminal (`openpty`/`forkpty`/Expect) tests rather than running the UI without a terminal.

| ID | Scenario | Pass criteria |
|---|---|---|
| TUI-01 | Monitor running sandbox | Dashboard renders status, resource bars, and process table |
| TUI-02 | Idle → busy → idle workload | Values update within documented refresh interval |
| TUI-03 | Press `q` | Clean exit, cursor restored, terminal no longer in raw mode |
| TUI-04 | Press Ctrl+C | Terminal restored; no orphaned helper threads/processes |
| TUI-05 | Resize terminal (including very small dimensions) | No crash/out-of-bounds rendering |
| TUI-06 | Sandbox stops during monitoring | Shows stopped/unavailable state, exits or remains usable as documented |
| TUI-07 | 10+ sandboxes/processes | Scrolling and selection remain correct; no flicker blocking input |
| TUI-08 | Missing metrics, zero limits, long names | Clear formatting, truncation, and no exceptions |

## Phase 8 — Security, reliability, fault injection

| ID | Scenario | Pass criteria |
|---|---|---|
| SEC-01 | Sandbox workload inspects its UID, capabilities, `NoNewPrivs` | Privileges match policy; not accidental host-root |
| SEC-02 | Block a harmless test syscall in a fixture using seccomp | Blocked syscall returns documented error, allowed syscalls still work |
| SEC-03 | Unprivileged user attempts privileged host operation | Denied; no host mutation |
| SEC-04 | Unauthorized user connects to daemon socket | Connection rejected/action unauthorized; no data leakage |
| SEC-05 | Malformed config: overflow, negative CPU, oversized memory, traversal names | Rejected before any host resource created |
| SEC-06 | Simultaneous create/delete/exec requests | State transitions atomic or serialized; no use-after-free or duplicate names |
| SEC-07 | Kill workload with SIGKILL / terminate daemon unexpectedly | Cgroup, mounts, socket, storage eventually reconciled and cleaned |
| SEC-08 | 100–1000 create/start/stop/delete cycles in isolated environment | No monotonically increasing leaked PIDs, mounts, fds, or cgroups; report measured counts |
| SEC-09 | Force errors for mkdir/write/fork/mount/socket operations | Errors propagated; partial allocations rolled back; no fail-open isolation |
| SEC-10 | Startup without cgroup delegation, unsupported namespace, or permission | Clear actionable error and no unisolated fallback |
| SEC-11 | Attempts to access host sentinel via unexpected paths / bind mounts / inherited descriptors | No unauthorized access under documented threat model; any limitation documented |
| SEC-12 | Fuzz CLI and daemon IPC parser with invalid/random input | No crashes or out-of-bounds memory access in supported tests |

**Security caveat:** Passing these tests demonstrates specific protections, not proof that hostile code is safely contained. A sandbox security claim needs a threat model and extensive review. Seccomp filters are not, by themselves, a complete sandbox.

## Recommended CTest organization

```text
tests/
  unit/
    cli_test.cpp
    config_test.cpp
    stats_parser_test.cpp
    state_machine_test.cpp
  integration/
    process_test.cpp
    cgroup_test.cpp
    namespace_test.cpp
    daemon_test.cpp
    storage_test.cpp
    monitoring_test.cpp
  e2e/
    lifecycle_test.sh
    tui_pty_test.py
  fixtures/
    cpu_spin.cpp
    memory_touch.cpp
    fork_orphan.cpp
    probe_pid.cpp
    probe_namespace.cpp
    syscall_probe.cpp
    rootfs/
```

Run common tests using:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run tests marked privileged only in an isolated, correctly configured test environment. Codex should implement CTest labels and capability prerequisites rather than guessing a cgroup path.

## Codex implementation instruction (paste into task)

> Treat this acceptance plan as the contract. For each phase, first add test fixtures and CTest cases, then implement the feature. Never declare a phase complete until relevant positive, negative, error-path, cleanup, and existing regression tests pass. Separate tests needing privileged Linux operations from unprivileged tests. If your environment lacks a delegated cgroup hierarchy or namespace permissions, mark those tests blocked/skipped with the exact prerequisite; do not fake a pass. For each phase report test IDs, commands, pass/fail/skip, any remaining risks, and commit SHA. Avoid broad host mutation and use disposable test environments for destructive operations.

## Primary references

- Linux cgroup v2: https://docs.kernel.org/admin-guide/cgroup-v2.html
- systemd cgroup delegation: https://systemd.io/CGROUP_DELEGATION/
- PID namespaces: https://man7.org/linux/man-pages/man7/pid_namespaces.7.html
- `pivot_root`: https://man7.org/linux/man-pages/man2/pivot_root.2.html
- seccomp: https://man7.org/linux/man-pages/man2/seccomp.2.html
- WSL systemd and lifecycle: https://learn.microsoft.com/en-us/windows/wsl/systemd
