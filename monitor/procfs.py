"""Small Linux /proc reader used to demonstrate OS-level data collection."""

from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True, slots=True)
class ProcfsSnapshot:
    cpu_total: float
    cpu_per_core: list[float]
    memory_percent: float
    memory_used: int
    memory_total: int
    swap_percent: float
    swap_used: int
    swap_total: int


class ProcfsReader:
    """Read CPU deltas from /proc/stat and memory totals from /proc/meminfo."""

    def __init__(self, root: Path = Path("/proc")) -> None:
        self.root = root
        self._previous_cpu: dict[str, tuple[int, int]] | None = None

    @staticmethod
    def read_file(path: Path) -> str:
        """Read a procfs file through POSIX file-descriptor operations.

        Python's os.open, os.read, and os.close are thin wrappers around the
        corresponding operating-system APIs. Keeping this operation explicit
        makes the system-call boundary visible for the project demonstration.
        """
        descriptor = os.open(os.fspath(path), os.O_RDONLY)
        chunks: list[bytes] = []
        try:
            while True:
                chunk = os.read(descriptor, 4096)
                if not chunk:
                    break
                chunks.append(chunk)
        finally:
            os.close(descriptor)
        return b"".join(chunks).decode("utf-8")

    @staticmethod
    def parse_cpu_stat(text: str) -> dict[str, tuple[int, int]]:
        samples: dict[str, tuple[int, int]] = {}
        for line in text.splitlines():
            fields = line.split()
            if not fields or not fields[0].startswith("cpu"):
                continue
            if fields[0] != "cpu" and not fields[0][3:].isdigit():
                continue
            values = [int(value) for value in fields[1:]]
            if len(values) < 4:
                continue
            idle = values[3] + (values[4] if len(values) > 4 else 0)
            samples[fields[0]] = (sum(values), idle)
        return samples

    @staticmethod
    def parse_meminfo(text: str) -> dict[str, int]:
        result: dict[str, int] = {}
        for line in text.splitlines():
            key, separator, remainder = line.partition(":")
            if not separator:
                continue
            fields = remainder.split()
            if fields and fields[0].isdigit():
                # Linux reports these values in KiB; expose bytes to callers.
                result[key] = int(fields[0]) * 1024
        return result

    @staticmethod
    def _percent(previous: tuple[int, int], current: tuple[int, int]) -> float:
        total_delta = current[0] - previous[0]
        idle_delta = current[1] - previous[1]
        if total_delta <= 0:
            return 0.0
        return max(0.0, min(100.0, 100.0 * (total_delta - idle_delta) / total_delta))

    def snapshot(self) -> ProcfsSnapshot | None:
        try:
            cpu_now = self.parse_cpu_stat(self.read_file(self.root / "stat"))
            memory = self.parse_meminfo(self.read_file(self.root / "meminfo"))
        except (OSError, ValueError):
            return None

        previous = self._previous_cpu
        self._previous_cpu = cpu_now
        if previous is None or "cpu" not in cpu_now or "cpu" not in previous:
            return None

        core_names = sorted(
            (name for name in cpu_now if name != "cpu"),
            key=lambda name: int(name[3:]),
        )
        cpu_per_core = [
            self._percent(previous[name], cpu_now[name])
            for name in core_names
            if name in previous
        ]

        memory_total = memory.get("MemTotal", 0)
        memory_available = memory.get("MemAvailable", memory.get("MemFree", 0))
        memory_used = max(0, memory_total - memory_available)
        memory_percent = 100.0 * memory_used / memory_total if memory_total else 0.0

        swap_total = memory.get("SwapTotal", 0)
        swap_used = max(0, swap_total - memory.get("SwapFree", 0))
        swap_percent = 100.0 * swap_used / swap_total if swap_total else 0.0

        return ProcfsSnapshot(
            cpu_total=self._percent(previous["cpu"], cpu_now["cpu"]),
            cpu_per_core=cpu_per_core,
            memory_percent=memory_percent,
            memory_used=memory_used,
            memory_total=memory_total,
            swap_percent=swap_percent,
            swap_used=swap_used,
            swap_total=swap_total,
        )
