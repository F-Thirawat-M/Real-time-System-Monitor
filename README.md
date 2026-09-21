# Mini-htop — Real-time System Monitor

A real-time terminal system monitor for an Operating Systems and System Calls
mini-project. It displays resource usage and running processes, and can send
termination signals to a selected process.

## Features

- Total and per-core CPU usage
- RAM, swap, and disk usage gauges
- Live process table with PID, owner, status, CPU%, and RAM%
- Search by process name or PID
- Sort by CPU, RAM, PID, or name
- Send `SIGTERM` or `SIGKILL` after confirmation
- Reads `/proc/stat` and `/proc/meminfo` with POSIX file-descriptor operations on Linux
- Falls back to `psutil` on Windows and macOS

## Architecture

```text
Textual terminal UI
        |
        +-- SystemMonitor
        |      +-- Linux: ProcfsReader -> /proc/stat, /proc/meminfo
        |      +-- Other platforms: psutil fallback
        |
        +-- ProcessMonitor
               +-- psutil process enumeration
               +-- POSIX signals on Linux/macOS
```

The UI refreshes once per second. Slow data collection runs in a worker thread so
the terminal remains responsive. On Linux, `ProcfsReader` takes two CPU samples and
calculates usage from the change in total and idle ticks.

## POSIX APIs and Linux interfaces

| API or interface | Purpose |
| --- | --- |
| `os.open()` | Open `/proc/stat` and `/proc/meminfo` and obtain a file descriptor |
| `os.read()` | Read kernel-provided resource data in chunks |
| `os.close()` | Release the file descriptor even when reading fails |
| `os.kill()` | Send `SIGTERM` or `SIGKILL` to the selected process |
| `/proc/stat` | Obtain total and per-core CPU time counters |
| `/proc/meminfo` | Obtain RAM and swap totals and availability |

Python's `os` functions are direct wrappers over operating-system APIs. `psutil`
is retained for process enumeration, disk statistics, and cross-platform fallback.

## Resource calculations

- **CPU:** compares two `/proc/stat` samples and calculates
  `(total_delta - idle_delta) / total_delta * 100`.
- **RAM:** calculates `MemTotal - MemAvailable` from `/proc/meminfo`.
- **Swap:** calculates `SwapTotal - SwapFree` from `/proc/meminfo`.
- **Disk:** uses `psutil.disk_usage()` for the system root.

## Project structure

```text
main.py                 Application entry point
monitor/procfs.py       POSIX file I/O and Linux /proc parsing
monitor/system_info.py  Resource snapshots and platform fallback
monitor/process_info.py Process listing, sorting, filtering, and signals
ui/app.py               Textual terminal interface
tests/                  Automated parser and POSIX file-I/O tests
docs/                   Report and demonstration material
```

## Install

Python 3.10 or newer is recommended.

### Windows PowerShell

```powershell
python -m venv .venv
.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
python main.py
```

### Linux / WSL

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements.txt
python main.py
```

Linux/WSL is recommended for the presentation because the application will read
CPU and memory data directly from the `/proc` virtual filesystem.

## Keyboard controls

| Key | Action |
| --- | --- |
| `/` | Search by process name or PID |
| `C` | Sort by CPU usage |
| `M` | Sort by RAM usage |
| `P` | Sort by PID |
| `N` | Sort by process name |
| `T` | Send `SIGTERM` to the selected process |
| `K` | Send `SIGKILL` to the selected process |
| `Space` | Pause or resume updates |
| `R` | Refresh immediately |
| `Q` | Quit |

## Safety

Run the monitor as a normal user. Only terminate test processes that you created
for the demonstration. `SIGTERM` allows a process to clean up; `SIGKILL` stops it
immediately and cannot be handled by the target process.

## Limitations

- Direct `/proc` collection is available only on Linux and WSL.
- Process enumeration and disk usage currently rely on `psutil`.
- A normal user can signal only processes for which the operating system grants
  permission.
- Process CPU percentages require multiple samples and may initially be zero.

## Testing

Run the automated tests with:

```bash
python -m unittest discover -s tests -v
```

The tests cover `/proc/stat`, `/proc/meminfo`, CPU delta calculations, and reading
files through POSIX file descriptors.

## Quick demo process

On Linux/WSL, create a harmless CPU workload:

```bash
python -c "while True: pass"
```

Open Mini-htop in another terminal, search for `python`, select the workload, and
send `SIGTERM`. Repeat with a new workload to demonstrate `SIGKILL`.
