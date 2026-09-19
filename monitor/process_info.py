"""Process listing, filtering, sorting, and signal handling."""

from __future__ import annotations

import os
import signal
from dataclasses import dataclass
from enum import Enum

import psutil


@dataclass(frozen=True, slots=True)
class ProcessInfo:
    pid: int
    username: str
    name: str
    status: str
    cpu_percent: float
    memory_percent: float


class SortMode(str, Enum):
    CPU = "cpu"
    MEMORY = "memory"
    PID = "pid"
    NAME = "name"


class ProcessMonitor:
    def __init__(self) -> None:
        # process_iter reuses Process instances, allowing cpu_percent to use deltas.
        for process in psutil.process_iter():
            try:
                process.cpu_percent(interval=None)
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                continue

    def list_processes(
        self,
        search: str = "",
        sort_mode: SortMode = SortMode.CPU,
    ) -> list[ProcessInfo]:
        needle = search.casefold().strip()
        processes: list[ProcessInfo] = []
        for process in psutil.process_iter(["pid", "name", "username", "status"]):
            try:
                info = process.info
                item = ProcessInfo(
                    pid=info["pid"],
                    username=info["username"] or "?",
                    name=info["name"] or "?",
                    status=info["status"] or "?",
                    cpu_percent=process.cpu_percent(interval=None),
                    memory_percent=process.memory_percent(),
                )
            except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
                continue
            if needle and needle not in item.name.casefold() and needle not in str(item.pid):
                continue
            processes.append(item)

        key_functions = {
            SortMode.CPU: lambda item: (-item.cpu_percent, item.pid),
            SortMode.MEMORY: lambda item: (-item.memory_percent, item.pid),
            SortMode.PID: lambda item: (item.pid,),
            SortMode.NAME: lambda item: (item.name.casefold(), item.pid),
        }
        return sorted(processes, key=key_functions[sort_mode])

    @staticmethod
    def send_signal(pid: int, force: bool = False) -> str:
        if pid == os.getpid():
            raise ValueError("Mini-htop cannot terminate itself")
        process = psutil.Process(pid)
        name = process.name()
        if force:
            if os.name == "posix":
                os.kill(pid, signal.SIGKILL)
            else:
                process.kill()
            return f"SIGKILL sent to {name} (PID {pid})"
        if os.name == "posix":
            os.kill(pid, signal.SIGTERM)
        else:
            process.terminate()
        return f"SIGTERM sent to {name} (PID {pid})"
