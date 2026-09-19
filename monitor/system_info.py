"""Collect CPU, memory, swap, and disk statistics."""

from __future__ import annotations

import os
import platform
from dataclasses import dataclass

import psutil

from .procfs import ProcfsReader


@dataclass(frozen=True, slots=True)
class SystemSnapshot:
    cpu_total: float
    cpu_per_core: list[float]
    memory_percent: float
    memory_used: int
    memory_total: int
    swap_percent: float
    swap_used: int
    swap_total: int
    disk_percent: float
    disk_used: int
    disk_total: int
    source: str


class SystemMonitor:
    """Return consistent snapshots, preferring Linux /proc when available."""

    def __init__(self, prefer_procfs: bool = True) -> None:
        self._procfs = ProcfsReader() if prefer_procfs and os.path.isdir("/proc") else None
        # Prime psutil so future non-blocking CPU readings contain useful deltas.
        psutil.cpu_percent(interval=None, percpu=True)

    @staticmethod
    def _disk_root() -> str:
        if platform.system() == "Windows":
            system_drive = os.environ.get("SystemDrive", "C:")
            return f"{system_drive}\\"
        return "/"

    def snapshot(self) -> SystemSnapshot:
        cpu_total: float
        cpu_per_core: list[float]
        memory_percent: float
        memory_used: int
        memory_total: int
        swap_percent: float
        swap_used: int
        swap_total: int
        source = "psutil"

        if self._procfs is not None:
            proc_snapshot = self._procfs.snapshot()
        else:
            proc_snapshot = None

        if proc_snapshot is not None:
            cpu_total = proc_snapshot.cpu_total
            cpu_per_core = proc_snapshot.cpu_per_core
            memory_percent = proc_snapshot.memory_percent
            memory_used = proc_snapshot.memory_used
            memory_total = proc_snapshot.memory_total
            swap_percent = proc_snapshot.swap_percent
            swap_used = proc_snapshot.swap_used
            swap_total = proc_snapshot.swap_total
            source = "/proc"
        else:
            cpu_per_core = psutil.cpu_percent(interval=None, percpu=True)
            cpu_total = sum(cpu_per_core) / len(cpu_per_core) if cpu_per_core else 0.0
            memory = psutil.virtual_memory()
            swap = psutil.swap_memory()
            memory_percent = memory.percent
            memory_used = memory.used
            memory_total = memory.total
            swap_percent = swap.percent
            swap_used = swap.used
            swap_total = swap.total

        disk = psutil.disk_usage(self._disk_root())
        return SystemSnapshot(
            cpu_total=cpu_total,
            cpu_per_core=cpu_per_core,
            memory_percent=memory_percent,
            memory_used=memory_used,
            memory_total=memory_total,
            swap_percent=swap_percent,
            swap_used=swap_used,
            swap_total=swap_total,
            disk_percent=disk.percent,
            disk_used=disk.used,
            disk_total=disk.total,
            source=source,
        )
