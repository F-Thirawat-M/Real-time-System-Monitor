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
- Reads `/proc/stat` and `/proc/meminfo` directly on Linux
- Falls back to `psutil` on Windows and macOS

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

## Quick demo process

On Linux/WSL, create a harmless CPU workload:

```bash
python -c "while True: pass"
```

Open Mini-htop in another terminal, search for `python`, select the workload, and
send `SIGTERM`. Repeat with a new workload to demonstrate `SIGKILL`.
