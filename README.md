# Mini-htop: Real-time System Monitor

Mini-htop is a Linux terminal program written in C for the Operating Systems and
System Calls Programming mini project. The **C program is the submission's main
implementation**. It reads Linux's `/proc` virtual filesystem and calls POSIX
APIs directly. The repository contains only the C implementation and its
supporting tests, demo, report and presentation.

## Requirements

- Linux or Ubuntu on WSL
- GCC or another C11 compiler, and `make`
- A terminal at least about 80 columns wide

The program has **no Python package dependencies**.

## Build and run on Linux / Ubuntu WSL

```bash
make
./mini-htop
```

For a non-interactive snapshot, useful for checking the build:

```bash
./mini-htop --once
```

From PowerShell, first enter Ubuntu with `wsl -d Ubuntu`, change to this project
directory under `/mnt/d/...`, then run the commands above. The project targets
Linux and does not run natively on Windows.

## Controls

| Key | Action |
| --- | --- |
| `/` | Search by process name or PID; Enter ends editing |
| `c`, `m`, `p`, `n` | Sort by CPU, RAM, PID, or name |
| Up/Down or `j`/`k` | Select a process |
| `t` | Confirm and send `SIGTERM` |
| `K` (uppercase) | Confirm and send `SIGKILL` |
| Space | Pause or resume collection |
| `r` | Refresh immediately |
| `q` | Quit |

`SIGTERM` asks a process to terminate and allows it to handle the signal.
`SIGKILL` immediately ends it and cannot be handled. Mini-htop rejects PID 1 and
its own PID, asks for confirmation, and checks the process start time before
signalling so a replaced PID is less likely to be targeted. A tiny race remains
between that check and `kill()`; do not use it to manage critical processes.
Run as an ordinary user, never as `sudo`, during the class demo.

## OS interfaces used

| Interface | Purpose |
| --- | --- |
| `open`, `read`, `close` | Read `/proc/stat`, `/proc/meminfo`, `/proc/[pid]/stat` |
| `opendir`, `readdir`, `closedir` | Enumerate `/proc` process directories |
| `stat`, `getpwuid` | Resolve process owner UID to a username |
| `statvfs` | Get root filesystem capacity and free space |
| `sysconf` | Read memory page size |
| `kill` | Send `SIGTERM` or `SIGKILL` |
| `termios`, `poll` | Read keyboard commands in an updating terminal UI |

`/proc` is Linux-specific, while most functions above are POSIX APIs. This
project intentionally targets Linux rather than claiming to be cross-platform.

## Test

```bash
make test
```

The C test checks system metrics, refuses to terminate Mini-htop's own process,
rejects a mismatched process start time, and sends `SIGTERM` and `SIGKILL` only
to child processes created by the test. It does not signal unrelated processes.

## Safe live demo

Build a disposable CPU workload:

```bash
make demo-load
./demo-load
```

Leave it running in its own terminal. In another terminal, run `./mini-htop`,
search for `demo-load`, select its PID, then press `t` and confirm with `y`.
The workload prints a graceful-shutdown message. Start it again, select the new
PID and use uppercase `K` to demonstrate that `SIGKILL` stops it without that
message. Confirm the PID before either action.

## Deliverables

- Source code: `c_src/`, `Makefile`, `tests/`, `demo/`
- Documentation: this README and `docs/REPORT.md`
- Presentation: `presentation/Mini-htop.pptx`

Add team names and course section to the report and presentation before
submission. Demonstrate and test on the same Linux/WSL environment used in class.
