# ABox

ABox is an experimental Linux-native sandbox manager. It uses Linux process
and cgroup APIs directly; it does not invoke Docker, LXC, or another runtime.

## Current implementation

The one-shot `run` command supports cgroup v2 CPU/memory limits and optional
cgroup statistics. `run --isolate` enables PID, mount, UTS, IPC, and user
namespace setup. The namespace child can make mounts private, become PID 1
for the workload, and (when a root filesystem is supplied) use `pivot_root`
and mount `/proc`. `run --rootfs DIR` enables mount and PID isolation while
using the selected directory as the root.

Namespace setup is opt-in because it requires kernel policy and, for a root
filesystem, privileges that are not available in every development
environment (including some WSL configurations). A failed setup aborts before
the workload is executed.

## Architecture decisions

- The existing synchronous execution path remains the compatibility path.
- Namespace setup happens after cgroup attachment and before `execvp`.
- The parent installs a one-entry uid/gid mapping for user namespaces; the
  child does not run a workload until mapping succeeds.
- A PID namespace's first process supervises and reaps the workload and any
  descendants, then returns the workload's shell-compatible status.
- Cgroup statistics are read before cgroup removal because controller files
  disappear when the group is removed.

## Verification

`cgroup_stats` and `namespace_supervisor` are unprivileged tests. Namespace
integration involving actual `CLONE_NEW*`, `pivot_root`, and `/proc` mounts
must be run manually on a disposable Linux host with the required delegated
permissions. Do not use a systemd-owned cgroup as the ABox base path.
