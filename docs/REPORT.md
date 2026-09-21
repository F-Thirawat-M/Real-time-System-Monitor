# Mini-htop: Real-time System Monitor

**Course:** Operating Systems and System Calls Programming  
**Students:** [เติมชื่อ-รหัสนักศึกษา]  
**Section / instructor:** [เติมข้อมูล]  
**Demonstration environment:** Ubuntu on WSL, or another Linux distribution

## 1. Project objective

Mini-htop is a terminal-based system monitor. It lets a user inspect CPU, RAM,
swap, disk and process usage in real time, find a process, and send a termination
signal. The implementation is in C and uses POSIX APIs on Linux.

## 2. Architecture

```text
Linux kernel
  /proc/stat, /proc/meminfo, /proc/[pid]/stat
  filesystem metadata and statvfs("/")
            |
            v
  c_src/monitor.c: collect and calculate snapshots
            |
            v
  c_src/main.c: render terminal UI and handle keyboard input
            |
            v
  user command -> confirmation -> identity check -> kill(pid, signal)
```

`monitor_collect()` creates a new snapshot and compares it with the previous
snapshot. `main.c` sorts, filters and displays processes. The UI refreshes at
roughly one-second intervals while not paused. On startup, CPU percentages need
a second sample, so their initial reading may be zero.

## 3. System calls and OS interfaces

| Function | Use in Mini-htop | Course connection |
| --- | --- | --- |
| `open()`, `read()`, `close()` | Read procfs files | File descriptors and file I/O |
| `opendir()`, `readdir()`, `closedir()` | Enumerate numeric `/proc` directories | Directory traversal |
| `stat()` | Read the owner UID of a process directory | File metadata and permissions |
| `getpwuid()` | Translate UID to username | User identity via libc/POSIX API |
| `statvfs()` | Get root filesystem space | Filesystem statistics |
| `sysconf()` | Get physical page size | System configuration |
| `kill()` | Send `SIGTERM` / `SIGKILL` | Signals and process management |
| `poll()`, `read()`, `termios` | Handle keyboard events | Terminal I/O |

Not every function above is itself a kernel syscall. For example, `getpwuid()`
is a POSIX library interface; `/proc` is a Linux virtual filesystem, not a
POSIX API. The code calls POSIX interfaces directly.

## 4. Data and calculations

### CPU

`/proc/stat` stores cumulative CPU ticks since boot. Mini-htop samples it
twice. For total CPU or each core:

```text
CPU usage (%) = 100 * (delta_total - delta_idle) / delta_total
```

Idle includes the Linux `idle` and `iowait` counters. Process CPU uses the
difference in `utime + stime` from `/proc/[pid]/stat` between snapshots. Its
percentage is normalized to one logical core, so a multi-threaded process can
exceed 100%. The program checks PID plus `starttime` when matching snapshots
to avoid treating a reused PID as the same process.

### Memory and swap

From `/proc/meminfo`:

```text
RAM used = MemTotal - MemAvailable
Swap used = SwapTotal - SwapFree
```

Process RAM percentage uses resident set size (`rss` pages) multiplied by the
page size from `sysconf(_SC_PAGESIZE)`, divided by total RAM.

### Disk

`statvfs("/")` reports filesystem blocks and free blocks. Mini-htop shows the
usage of the root filesystem, not a sum across every mounted volume.

## 5. Process management and safety

The user selects a row and presses `t` or uppercase `K`. The program asks for
confirmation, remembers the selected PID and process start time, rereads the
identity, then calls `kill()`. A failed call shows the OS error, such as
`Permission denied` or `No such process`. The program refuses PID 1 and its
own PID. The process could theoretically change between the identity check and
`kill()`, so this is a safety measure rather than an absolute guarantee.

`SIGTERM` is catchable and lets the demo workload clean up. `SIGKILL` cannot be
caught, blocked, or ignored by the target. During the presentation, only signal
the disposable `demo-load` process created for the demo, and run Mini-htop
without `sudo`.

## 6. Build, tests and demo

```bash
make
make test
make demo-load
./mini-htop
```

`make test` compiles the C program and runs a smoke snapshot plus tests for
metrics and signal safety. The signal tests create their own children with
`fork()` and reap them with `waitpid()`; no unrelated process is terminated. For a live
demo, run `./demo-load` in one terminal and `./mini-htop` in another. Filter by
`demo-load`, show its high CPU usage, then send `SIGTERM` and later `SIGKILL`
to separate demo instances.

## 7. Limitations and future work

- Linux-only because the data source is `/proc`.
- The screen shows as many CPU cores as fit in three terminal lines; collection
  supports up to 256 cores.
- Process names in `/proc/[pid]/stat` may be truncated by the kernel.
- A process can exit during a snapshot; the program skips unreadable entries.
- Disk usage covers only the root filesystem.
- Signals to processes owned by other users normally fail without permission.
- For a future version, use Linux `pidfd_send_signal()` to eliminate the narrow
  PID-reuse race, and add a more flexible per-core layout.

## 8. Conclusion

The project demonstrates process inspection, Linux virtual files, filesystem
statistics, terminal I/O and signals through a working C program. Its core
functionality can be shown live using a safe workload and verified with the
included test target.
