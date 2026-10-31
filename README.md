# ABox

**A Linux sandbox runtime built from first principles in C++20.**

ABox is an experimental Linux process-isolation and workload-management project. It uses Linux system calls and kernel interfaces directly—`fork`, `execvp`, `waitpid`, namespaces, and **cgroups v2**—instead of delegating execution to Docker or another container runtime.

The project has two execution paths:

- **`abox run`** launches a one-shot workload with optional CPU and memory limits, resource accounting, and namespace isolation.
- **`aboxd`** is a long-running daemon that accepts lifecycle commands over a Unix domain socket and manages named workloads.

> **Project status:** Experimental, through the initial daemon/lifecycle milestone (Phase 4). ABox is intended for learning and systems experimentation, **not** for safely executing untrusted code. The two execution paths do not yet offer identical isolation: the daemon currently manages cgroup-backed host-namespace processes, while namespace and root-filesystem isolation are implemented in the one-shot `run` path.

## Contents

- [Why ABox?](#why-abox)
- [Features](#features)
- [Requirements](#requirements)
- [Build and test](#build-and-test)
- [Installation and setup](#installation-and-setup)
- [Quick start: one-shot execution](#quick-start-one-shot-execution)
- [Quick start: daemon-managed workloads](#quick-start-daemon-managed-workloads)
- [Architecture](#architecture)
- [How execution works](#how-execution-works)
- [Kernel mechanisms](#kernel-mechanisms)
- [State, IPC, and lifecycle](#state-ipc-and-lifecycle)
- [Testing](#testing)
- [Implementation status and limitations](#implementation-status-and-limitations)
- [Roadmap](#roadmap)
- [References](#references)

## Why ABox?

A Linux process normally shares the host's view of process IDs, mounts, hostname, and other resources. Running a program through `fork()` and `execvp()` is easy; running it with **controlled resources, separate system views, observable behavior, and a managed lifecycle** takes considerably more work.

ABox explores those mechanisms as small, independently understandable components. The objective is to understand how a container-like runtime interacts with the Linux kernel: how a workload starts, what it can see, how the kernel accounts for its resources, and how the runtime eventually cleans it up.

## Features

| Capability | One-shot `abox run` | Daemon-managed `aboxd` |
| --- | --- | --- |
| Launch an arbitrary executable | Yes | Yes (`start` / `exec`) |
| cgroups v2 process placement | Yes | Yes |
| Configurable CPU quota / memory maximum | Yes | Not exposed by the current daemon CLI |
| Final CPU, memory, and OOM statistics | `--stats` | Not exposed by the current daemon CLI |
| UTS, PID, mount, user, and IPC namespaces | Optional | Not yet integrated |
| Root filesystem switch with `pivot_root` | Optional / experimental | Not yet integrated |
| Named workloads and lifecycle commands | No | Yes |
| Unix domain socket IPC | No | Yes |
| On-disk workload configuration | No | Yes (basic metadata) |

ABox does **not** currently implement network namespace isolation, container images, storage quotas, seccomp policies, a terminal UI, or a production-grade security boundary.

## Requirements

- Linux with **cgroup v2** (`cgroup2`) mounted.
- A C++20 compiler, CMake **3.16+**, and standard Linux development headers.
- Access to a **properly delegated cgroup subtree** with the required controllers enabled. Creating workload cgroups and writing their control files is permission-sensitive.
- Kernel support and the appropriate permissions for any requested namespaces or mounts.

Development has been carried out on **Ubuntu under WSL2**. Some namespace and mount operations depend on WSL configuration; privileged integration testing is best performed in a disposable Linux VM or test environment.

### Cgroup setup

The default cgroup parent in the current code is:

```text
/sys/fs/cgroup/abox
```

ABox expects this parent to **already exist** and allow creation of child cgroups. You can override it for both the CLI and daemon:

```bash
export ABOX_CGROUP_BASE=/path/to/your/delegated/cgroup
```

Check the available and enabled controllers at the *parent*:

```bash
cat "$ABOX_CGROUP_BASE/cgroup.controllers"
cat "$ABOX_CGROUP_BASE/cgroup.subtree_control"
cat "$ABOX_CGROUP_BASE/cgroup.procs"
```

For CPU and memory limits, `cpu` and `memory` must be available and enabled for children. In cgroups v2, enabling domain controllers on a non-root parent normally requires that no processes reside directly in that parent (the **no-internal-process constraint**).

**Do not change systemd-owned cgroups arbitrarily.** Use systemd delegation for routine development, or a dedicated, disposable environment for privileged experiments. Also avoid running untrusted workloads under `sudo`: doing so launches those programs with elevated host privileges.

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

This builds two primary executables:

```text
build/abox     # Command-line client and one-shot runner
build/aboxd    # Daemon for named workloads
```

Confirm that the CLI works:

```bash
./build/abox --help
./build/abox --version
```

## Installation and setup

### Install via Cloudsmith (Ubuntu / Debian)

ABox is distributed as a Debian package. First, **configure the public ABox
Cloudsmith APT repository** on your machine:

**No Cloudsmith account or login is required to install ABox from a public
repository.** First, add the ABox APT repository and its signing key once.
Review the setup script before running it with elevated privileges:

```bash
curl -fsSL \
  'https://dl.cloudsmith.io/public/abox/abox/cfg/setup/bash.deb.sh' \
  -o /tmp/abox-cloudsmith-setup.sh
less /tmp/abox-cloudsmith-setup.sh
sudo bash /tmp/abox-cloudsmith-setup.sh
```

This uses Cloudsmith's documented public Debian repository setup URL pattern
for the `abox/abox` workspace and repository. The repository must be set to
**public** in Cloudsmith for anonymous installation to work. [ABox v0.1.0 package
page](https://app.cloudsmith.com/abox/r/abox/deb/abox/0.1.0/x86beL0FPP7q).

After the repository has been added, install **ABox v0.1.0**:

```bash
sudo apt-get update
sudo apt-get install abox=0.1.0
```

Verify the installation:

```bash
abox --version
command -v abox
command -v aboxd
```

If APT cannot find version `0.1.0`, check whether the repository is configured
for your Ubuntu/Debian release and architecture, and inspect the available
package versions:

```bash
apt-cache policy abox
```

To provision the default Ubuntu sandbox root filesystem, explicitly run:

```bash
sudo abox setup --dry-run
sudo abox setup
```

Package installation alone does **not** grant namespace permissions or
configure cgroup delegation; see [Requirements](#requirements) before running
isolated workloads.

### Build and install from source

```bash
cmake -S . -B build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build-release -j
```

To inspect the installation layout without changing the host:

```bash
DESTDIR="$PWD/stage" cmake --install build-release
```

To install the binaries and packaged configuration onto the host instead:

```bash
sudo cmake --install build-release
```

ABox also supports CPack-generated Debian and TGZ packages. These packages
install the ABox binaries and supporting files, **not** a complete root
filesystem. Setup is a separate, explicit operation; it preserves an existing
valid rootfs and refuses to overwrite an existing non-empty incomplete one.
Provisioning requires supported Ubuntu tooling (including `debootstrap`),
network access if required, and appropriate permissions.

### Default rootfs selection

When `--isolate` is used, ABox resolves the rootfs in this order:

1. Explicit `--rootfs DIR`;
2. `ABOX_ROOTFS` environment variable;
3. `/etc/abox/abox.conf`;
4. `/var/lib/abox/images/default`.

The selected path must exist and be a directory. Individual namespace flags,
such as `--uts` or `--pid`, do not implicitly select a rootfs.

Some integration tests require a delegated cgroup subtree and namespace
capabilities. Tests lacking those prerequisites may report **skipped**, not
passed; see [Testing](#testing).

## Quick start: one-shot execution

The `run` command executes a program and returns its exit status:

```bash
./build/abox run -- /bin/echo 'Hello from ABox'
```

Limit CPU time and memory, then print a summary when the program finishes:

```bash
./build/abox run \
  --cpus 0.5 \
  --memory 256M \
  --stats \
  -- /bin/sleep 2
```

For this to succeed, ABox must be able to create and configure cgroups beneath `ABOX_CGROUP_BASE`.

### One-shot options

| Option | Meaning |
| --- | --- |
| `--cpus N` | CPU-time quota in CPU-equivalents (for example, `0.5` or `2`) |
| `--memory SIZE` | Hard memory maximum; supported suffixes: `M` (MiB) or `G` (GiB) |
| `--stats` | Print resource usage after execution |
| `--uts` | Create a new hostname/UTS namespace |
| `--pid` | Launch a workload through a new PID namespace supervisor |
| `--mount` | Create a separate mount namespace and make mounts private |
| `--user` | Create a user namespace and configure a single UID/GID mapping |
| `--ipc` | Create a separate IPC namespace |
| `--isolate` | Enable PID, mount, UTS, user, and IPC namespace setup together |
| `--rootfs DIR` | Attempt a root filesystem switch using `pivot_root` (also enables PID/mount setup) |
| `--` | End ABox options; everything afterward is the executable and its arguments |

For example, with the required namespace privileges:

```bash
./build/abox run --uts -- /bin/hostname
# Expected workload hostname: abox
```

To request the full set of implemented namespaces:

```bash
./build/abox run --isolate -- /bin/hostname
```

To attempt execution against a prepared root filesystem:

```bash
./build/abox run --rootfs /absolute/path/to/rootfs -- /bin/sh
```

**Rootfs prerequisites:** The directory must exist and contain the program, its interpreter and shared libraries (if dynamically linked), and a usable `/proc` directory. `pivot_root` and mount operations require suitable permissions. The current `/proc` setup needs further hardening/validation for PID namespace correctness; do not treat this mode as a verified security boundary.

## Quick start: daemon-managed workloads

The daemon accepts simple text commands over a Unix domain socket. It keeps per-workload metadata in a state directory and launches independent workload processes.

### 1. Start the daemon

Run this in one terminal:

```bash
export ABOX_CGROUP_BASE=/path/to/your/delegated/cgroup
export ABOX_SOCKET="/tmp/aboxd-$(id -u).sock"
export ABOX_STATE_DIR="$HOME/.local/state/abox"

./build/aboxd --state "$ABOX_STATE_DIR" --socket "$ABOX_SOCKET"
```

`aboxd` currently stays in the foreground. Keep this process running while issuing commands in another terminal. The state directory and socket are configurable; if unspecified, the defaults are `/tmp/abox-state-<uid>` and `/tmp/aboxd-<uid>.sock`.

### 2. Use the CLI

In a second terminal, point the CLI at the same socket:

```bash
export ABOX_SOCKET="/tmp/aboxd-$(id -u).sock"

./build/abox create demo
./build/abox list
./build/abox start demo -- /bin/sleep 120
./build/abox list
./build/abox exec demo -- /usr/bin/id
./build/abox stop demo
# Wait for the process to exit and for the daemon to observe it.
./build/abox list
./build/abox delete demo
```

`start` and `exec` return an `OK <pid>` response when the daemon has launched the process. **`exec` is currently a launch operation, not an interactive terminal attachment:** workload stdout/stderr are not streamed back through the CLI socket.

The daemon's command parser currently splits on whitespace, so quoted arguments and paths containing spaces are **not** reliably preserved. Keep these examples to simple arguments.

### Lifecycle semantics today

| Command | What the current daemon does |
| --- | --- |
| `create <name>` | Creates a state directory with a basic `config` file |
| `list` | Scans state directories and reports created/running/stopped states |
| `start <name> -- <program> ...` | Creates a cgroup, starts a process, records its PID |
| `exec <name> -- <program> ...` | Starts an additional process in the named cgroup, **without entering an existing namespace** |
| `stop <name>` | Sends `SIGTERM` to the recorded process group |
| `delete <name>` | Removes state for a workload that is not reported running |

A created sandbox is currently **metadata**, not an already-running isolated container. The daemon does not yet save CPU/memory/namespace configuration or create a persistent namespace-init process during `create`. The daemon's `start` and `exec` path is separate from the namespace-aware one-shot runner.

## Architecture

```text
                         +----------------------+
                         |       abox CLI       |
                         +-----------+----------+
                                     |
                    +----------------+----------------+
                    |                                 |
                 abox run                      lifecycle commands
                    |                                 |
                    v                                 v
           +-----------------+                Unix domain socket
           | Process runner  |                         |
           +--------+--------+                         v
                    |                         +-----------------+
          +---------+---------+               |      aboxd      |
          |                   |               | state + process |
          v                   v               |   management    |
     CgroupManager      Namespace setup      +--------+--------+
          |                   |                        |
          v                   v                        v
      cgroups v2       Linux namespaces          cgroups v2
          \                   /                        |
           \                 /                         v
            +-- workload ---+                 host-namespace workload
```

### Source layout

```text
src/
  main.cpp         CLI parsing and daemon client IPC
  process.hpp/.cpp one-shot execution, synchronization, exit status, stats
  cgroup.hpp/.cpp cgroup lifecycle, resource controls, metric parsing
  namespace.hpp/.cpp namespace setup, UID/GID mapping, outer PID supervisor
  config.hpp/.cpp rootfs configuration and precedence rules
  setup.hpp/.cpp  explicit rootfs provisioning and prerequisite checks
  daemon.cpp       Unix socket server, named state, workload lifecycle

tests/
  stats_test.cpp          cgroup-statistics parsing
  namespace_test.cpp      supervisor unit test
  cli_acceptance.cpp      CLI regression checks
  namespace_acceptance.cpp namespace integration checks
  daemon_acceptance.cpp    daemon and lifecycle checks
  ACCEPTANCE_TESTS.md     expanded phase-by-phase test specification

CMakeLists.txt
version.hpp.in
```

## How execution works

### One-shot execution (`abox run`)

The one-shot runner performs the following sequence:

```text
Parse options and program arguments
   |
Create a unique cgroup (run-<parent-pid>)
   |
Write cpu.max / memory.max when requested
   |
Create synchronization pipes, then fork()
   |
   +-- Parent: move child PID into cgroup.procs
   |             |
   |             +-- signal child to continue
   |             +-- if needed, write UID/GID mappings
   |             +-- waitpid(), collect final statistics, remove cgroup
   |
   +-- Child:  block until parent authorizes execution
                 |
                 +-- unshare requested namespaces
                 +-- coordinate user namespace mapping
                 +-- configure mounts, rootfs, hostname
                 +-- if PID isolation: fork workload into the new PID namespace
                 +-- otherwise: execvp(program, argv)
```

**Why the synchronization pipe?** After `fork()`, Linux does not guarantee whether the parent or child runs first. Without the pipe, the child could execute before ABox attaches it to its cgroup. The child therefore blocks until the parent has attached its PID; an attachment failure prevents execution.

**Why `execvp()`?** It replaces the child process image with the requested program without creating another ordinary wrapper process. ABox uses conventional exit-status handling: a normal exit returns the process's code, and signal termination is represented as `128 + signal number`.

**Why the PID supervision step?** `unshare(CLONE_NEWPID)` affects subsequently created children, not the calling process. The current `supervise()` function forks the **workload itself** into the new PID namespace, where that workload becomes PID 1; the outer parent, which remains outside the new PID namespace, waits and returns its exit status. There is **not yet a dedicated init process inside the namespace** to forward signals or reap arbitrary orphaned descendants. This distinction is important for PID-namespace correctness and reliability.

### Daemon execution (`aboxd`)

The daemon's current execution sequence is different:

```text
CLI --> connect(AF_UNIX) --> send a newline-terminated command
                               |
                               v
                            aboxd
                               |
                 state lookup / create or update
                               |
                    fork() + setsid() + execvp()
                               |
                 attach spawned PID to named cgroup
                               |
                 send OK / ERR response to client
```

The Unix socket is created with filesystem permissions `0600` and the daemon state directory is set to `0700`. The server uses a simple line-oriented protocol; it does not yet support authenticated multi-user access, structured argument framing, terminal I/O forwarding, or joining another workload's namespaces.

Unlike the one-shot path, **the daemon currently attaches the child after `fork()` without holding it behind an execution gate**. That leaves a race in which the process could begin executing before the cgroup attachment completes. This must be fixed before treating daemon-managed resource limits as strictly enforced from first instruction.

## Kernel mechanisms

### 1. cgroups v2: resource control

A cgroup is represented by a directory under the cgroup v2 filesystem. Linux exposes its controls and counters as virtual files:

```text
<ABOX_CGROUP_BASE>/
  run-<pid>/
    cgroup.procs
    cpu.max
    cpu.stat
    memory.max
    memory.current
    memory.peak
    memory.events

  sandbox-<name>/
    cgroup.procs
    ...
```

ABox translates CPU-equivalents into CFS bandwidth quota with a **100,000 µs period**:

```text
--cpus 0.5  ->  cpu.max = 50000 100000
--cpus 2    ->  cpu.max = 200000 100000
```

Memory limits are expressed in bytes:

```text
--memory 256M  ->  memory.max = 268435456
--memory 1G    ->  memory.max = 1073741824
```

These are **limits, not reservations**. CPU quotas constrain execution time, not CPU affinity. `memory.max` caps memory charges and may cause reclamation or cgroup OOM handling when exceeded.

### 2. Resource statistics

With `--stats`, the runner reads the cgroup's counters **before it removes the cgroup**:

| Source | Data |
| --- | --- |
| `cpu.stat` | CPU usage in microseconds; quota throttling count and time, when available |
| `memory.current` | Current memory charges at sampling time |
| `memory.peak` | Peak accounted memory, when supported |
| `memory.events` | Memory limit, OOM, and OOM-kill events |
| `std::chrono::steady_clock` | Elapsed wall-clock duration |

Average CPU consumption is reported in **CPU-equivalents**:

```text
average CPU cores = CPU usage seconds / elapsed wall seconds
```

This can exceed `1.0` when the workload uses multiple CPU cores. Missing optional kernel counters are represented as unavailable rather than silently being treated as zero.

### 3. Namespaces: separate system views

| Namespace | Linux flag | Purpose |
| --- | --- | --- |
| UTS | `CLONE_NEWUTS` | Separate hostname |
| PID | `CLONE_NEWPID` | Separate process ID numbering for subsequently created children |
| Mount | `CLONE_NEWNS` | Separate mount table and private mount propagation |
| User | `CLONE_NEWUSER` | Namespace-local user IDs and capabilities; mapped back to a host UID/GID |
| IPC | `CLONE_NEWIPC` | Separate System V IPC and POSIX message-queue namespace |

The user namespace mapping is coordinated with the parent process using pipes and `/proc/<pid>/uid_map`, `/proc/<pid>/gid_map`, and `setgroups`. This allows the child to wait until its mappings are configured before continuing.

The rootfs path bind-mounts the selected directory, attempts `pivot_root`, detaches the old root, and mounts `/proc`. This is **experimental**. In particular, mount timing relative to entry into the new PID namespace needs verification; a fresh procfs mounted from the wrong PID namespace may expose an incorrect process view.

### 4. Process lifecycle

ABox uses `waitpid()` to observe direct children, converts process termination into shell-style status codes, and attempts to remove empty workload cgroups.

The daemon records each primary workload PID in a file and uses it to infer state. It also attempts child reaping during its request loop. This is a basic lifecycle model: it is **not yet** a robust durable process supervisor capable of complete recovery from daemon crashes, PID reuse, orphaned descendants, or partial transitions.

## State, IPC, and lifecycle

The daemon's state directory resembles:

```text
<ABOX_STATE_DIR>/
  demo/
    config      # currently version=1
    pid         # present while a primary workload is recorded as active
```

The Unix socket handles requests such as:

```text
create demo
list
start demo -- /bin/sleep 120
stop demo
delete demo
```

ABox currently sends a single newline-terminated request and receives an `OK`, `ERR`, or listing response. The wire format is deliberately simple for this milestone and is not a stable public API.

By default the state is stored under `/tmp`, which may be cleared across reboots. Specify `--state` to use a persistent directory. **Configuration persistence is not process persistence:** workloads do not survive shutdown of the WSL/Linux instance.

## Testing

Run all registered CTest tests:

```bash
ctest --test-dir build --output-on-failure
```

Inspect registered tests and labels:

```bash
ctest --test-dir build -N
ctest --test-dir build -L phase3 --output-on-failure
ctest --test-dir build -L phase4 --output-on-failure
ctest --test-dir build -L phaseA --output-on-failure
ctest --test-dir build -L phaseB --output-on-failure
ctest --test-dir build -L phaseC --output-on-failure
ctest --test-dir build -L phaseD --output-on-failure
```

Current test programs cover statistics parsing, a namespace supervisor check, CLI behavior, rootfs configuration precedence, setup safety, staged installation, DEB metadata/content, namespace integration checks, and daemon lifecycle behavior. Namespace tests are labeled `requires_privileges`; the daemon integration test may also skip the workload-lifecycle portion if `ABOX_CGROUP_BASE` is not provided. CTest uses **exit code 77** for supported skips.

The larger test inventory in [`tests/ACCEPTANCE_TESTS.md`](tests/ACCEPTANCE_TESTS.md) is an **acceptance specification**, not proof that every scenario has already been automated or passed. In particular, test results from one Linux/WSL environment do not establish security guarantees on every host.

## Implementation status and limitations

ABox is a systems-learning project with working kernel-facing components, but several implementation details are intentionally unfinished:

1. **Two separate execution paths.** `abox run` supports namespaces and rootfs setup; `aboxd` currently starts processes in the daemon's host namespaces and does not join a persistent sandbox's namespace set.
2. **Basic daemon lifecycle.** Daemon state is primarily a saved PID and metadata directory; restart recovery, PID reuse protection, graceful-stop timeouts, signal forwarding, and descendant cleanup need strengthening.
3. **Daemon startup race.** Unlike the one-shot path, daemon workloads may execute before cgroup attachment succeeds.
4. **Minimal IPC framing.** Socket commands are whitespace-delimited; quoting, spaces in arguments, interactive I/O, streaming, and structured responses are not implemented. The current CLI also treats non-`OK` list responses as nonzero exit status even when it prints the listing.
5. **Rootfs/procfs limitations.** `pivot_root` and `/proc` setup require privilege and correctness testing; mount/proc placement should not be taken as verified isolation.
6. **Incomplete security boundary.** There is no seccomp policy, capability drop policy, dedicated network namespace, or comprehensive host-filesystem escape testing. Do not use ABox for hostile workloads.
7. **PID 1 is currently the workload.** The outer supervisor is not a namespace-local init; signal and orphan reaping semantics need explicit handling before calling PID isolation robust.
8. **No persistent storage backend.** The daemon persists metadata, not per-sandbox filesystem images, storage quotas, or copy-on-write layers.
9. **No live metrics service or TUI.** The implemented stats path is a post-execution summary for one-shot workloads.
10. **Setup is deliberately explicit.** It does not create cgroup delegation, enable controllers, install packages, or provision a rootfs unless the user invokes the non-dry-run setup command.
11. **Packaging is not a security boundary.** A shared default rootfs has no per-sandbox writable storage, copy-on-write layer, or storage quota.

These limitations are tracked deliberately: avoiding false isolation guarantees is part of building a reliable runtime.

## Roadmap

- **Phase 0–2:** C++ CLI, process execution, cgroups v2 limits, and end-of-run statistics — implemented.
- **Phase 3:** UTS, PID, mount, user, and IPC namespace plumbing, plus experimental rootfs support — implemented as an initial one-shot execution path; further validation/hardening required.
- **Phase 4:** Unix-socket daemon, named workloads, basic state and lifecycle — initial implementation complete; lifecycle and namespace integration require follow-up.
- **Installation/configuration:** standard CMake installation, rootfs precedence, explicit setup diagnostics, and DEB/TGZ packaging — implemented; provisioning remains an explicit privileged operation.
- **Phase 5:** Persistent per-sandbox storage, filesystem capacity limits, and optionally OverlayFS/copy-on-write.
- **Phase 6:** Continuous CPU/memory/I/O telemetry and per-sandbox sampling.
- **Phase 7:** An interactive terminal dashboard for sandbox inspection.
- **Phase 8:** Daemon recovery, stronger supervision, capabilities, seccomp, authorization, and fault-injection testing.

## References

- [Linux kernel: Control Group v2](https://docs.kernel.org/admin-guide/cgroup-v2.html)
- [systemd: Control Group Delegation](https://systemd.io/CGROUP_DELEGATION/)
- [Linux manual: namespaces(7)](https://man7.org/linux/man-pages/man7/namespaces.7.html)
- [Linux manual: pid_namespaces(7)](https://man7.org/linux/man-pages/man7/pid_namespaces.7.html)
- [Linux manual: user_namespaces(7)](https://man7.org/linux/man-pages/man7/user_namespaces.7.html)
- [Linux manual: pivot_root(2)](https://man7.org/linux/man-pages/man2/pivot_root.2.html)

---

**ABox is a learning-oriented systems project, not a replacement for a production container runtime.** The emphasis is on understanding and validating the Linux mechanisms that make isolation, resource control, and workload supervision possible.
