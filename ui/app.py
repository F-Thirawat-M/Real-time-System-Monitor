"""Interactive Textual interface for Mini-htop."""

from __future__ import annotations

import asyncio

import psutil
from textual.app import App, ComposeResult
from textual.containers import Horizontal, Vertical
from textual.screen import ModalScreen
from textual.widgets import Button, DataTable, Footer, Header, Input, Label, Static

from monitor.process_info import ProcessMonitor, SortMode
from monitor.system_info import SystemMonitor


def human_bytes(value: int) -> str:
    size = float(value)
    for unit in ("B", "KiB", "MiB", "GiB", "TiB"):
        if size < 1024.0 or unit == "TiB":
            return f"{size:.1f} {unit}"
        size /= 1024.0
    return f"{size:.1f} TiB"


def gauge(label: str, percent: float, used: int | None = None, total: int | None = None) -> str:
    width = 18
    filled = round(max(0.0, min(100.0, percent)) * width / 100)
    bar = "█" * filled + "░" * (width - filled)
    amount = ""
    if used is not None and total is not None:
        amount = f"  {human_bytes(used)} / {human_bytes(total)}"
    return f"[bold]{label:<5}[/bold] [{bar}] [bold]{percent:5.1f}%[/bold]{amount}"


class ConfirmSignalScreen(ModalScreen[bool]):
    def __init__(self, pid: int, force: bool) -> None:
        super().__init__()
        self.pid = pid
        self.force = force

    def compose(self) -> ComposeResult:
        signal_name = "SIGKILL" if self.force else "SIGTERM"
        with Vertical(id="confirm-dialog"):
            yield Label(f"Send {signal_name} to PID {self.pid}?", id="confirm-question")
            yield Label(
                "SIGKILL cannot be handled or ignored by the target process."
                if self.force
                else "SIGTERM asks the process to shut down gracefully.",
                id="confirm-help",
            )
            with Horizontal(id="confirm-buttons"):
                yield Button("Cancel", variant="default", id="cancel")
                yield Button("Confirm", variant="error", id="confirm")

    def on_button_pressed(self, event: Button.Pressed) -> None:
        self.dismiss(event.button.id == "confirm")


class MiniHtopApp(App[None]):
    TITLE = "Mini-htop — Real-time System Monitor"
    SUB_TITLE = "Operating Systems and System Calls"
    CSS = """
    Screen { background: #0b1020; color: #e6edf7; }
    Header { background: #13203b; color: #7dd3fc; }
    #resources { height: auto; padding: 1 2; background: #10182b; }
    .resource-column { width: 1fr; height: auto; padding: 0 1; }
    .gauge { height: 2; color: #bfe8ff; }
    #cores { height: auto; max-height: 6; color: #a7f3d0; }
    #search { margin: 0 2; border: tall #38bdf8; }
    #process-table { height: 1fr; margin: 0 1; }
    #status { height: 1; padding: 0 2; background: #13203b; color: #cbd5e1; }
    Footer { background: #13203b; }
    ConfirmSignalScreen { align: center middle; background: rgba(2, 6, 23, 0.75); }
    #confirm-dialog { width: 58; height: 12; padding: 1 2; background: #172033; border: round #fb7185; }
    #confirm-question { text-align: center; height: 2; text-style: bold; }
    #confirm-help { text-align: center; height: 3; color: #cbd5e1; }
    #confirm-buttons { align: center middle; height: 3; }
    #confirm-buttons Button { margin: 0 1; }
    """
    BINDINGS = [
        ("q", "quit", "Quit"),
        ("/", "search", "Search"),
        ("c", "sort_cpu", "CPU sort"),
        ("m", "sort_memory", "RAM sort"),
        ("p", "sort_pid", "PID sort"),
        ("n", "sort_name", "Name sort"),
        ("t", "terminate", "SIGTERM"),
        ("k", "kill", "SIGKILL"),
        ("space", "pause", "Pause"),
        ("r", "refresh_now", "Refresh"),
    ]

    def __init__(self) -> None:
        super().__init__()
        self.system_monitor = SystemMonitor()
        self.process_monitor = ProcessMonitor()
        self.sort_mode = SortMode.CPU
        self.search_text = ""
        self.paused = False
        self.refreshing = False
        self._opening_search = False

    def compose(self) -> ComposeResult:
        yield Header(show_clock=True)
        with Horizontal(id="resources"):
            with Vertical(classes="resource-column"):
                yield Static(id="cpu", classes="gauge")
                yield Static(id="memory", classes="gauge")
            with Vertical(classes="resource-column"):
                yield Static(id="swap", classes="gauge")
                yield Static(id="disk", classes="gauge")
        yield Static(id="cores")
        yield Input(placeholder="Search by process name or PID; press Enter to close", id="search")
        yield DataTable(id="process-table", cursor_type="row", zebra_stripes=True)
        yield Static("Starting monitor…", id="status")
        yield Footer()

    async def on_mount(self) -> None:
        table = self.query_one("#process-table", DataTable)
        table.add_columns("PID", "USER", "PROCESS", "STATUS", "CPU %", "RAM %")
        table.fixed_columns = 1
        self.query_one("#search", Input).display = False
        table.focus()
        await self.update_monitor()
        self.set_interval(1.0, self.update_monitor)

    def collect_data(self):
        """Collect slow OS data away from Textual's UI event loop."""
        snapshot = self.system_monitor.snapshot()
        processes = self.process_monitor.list_processes(self.search_text, self.sort_mode)
        return snapshot, processes

    async def update_monitor(self) -> None:
        if self.paused or self.refreshing:
            return
        self.refreshing = True
        try:
            snapshot, processes = await asyncio.to_thread(self.collect_data)
            self.query_one("#cpu", Static).update(gauge("CPU", snapshot.cpu_total))
            self.query_one("#memory", Static).update(
                gauge("RAM", snapshot.memory_percent, snapshot.memory_used, snapshot.memory_total)
            )
            self.query_one("#swap", Static).update(
                gauge("SWAP", snapshot.swap_percent, snapshot.swap_used, snapshot.swap_total)
            )
            self.query_one("#disk", Static).update(
                gauge("DISK", snapshot.disk_percent, snapshot.disk_used, snapshot.disk_total)
            )
            core_text = "  ".join(
                f"C{index} {percent:4.0f}%"
                for index, percent in enumerate(snapshot.cpu_per_core)
            )
            self.query_one("#cores", Static).update(f"[bold]Cores:[/bold] {core_text}")

            table = self.query_one("#process-table", DataTable)
            old_cursor = table.cursor_row
            table.clear()
            for process in processes:
                table.add_row(
                    str(process.pid),
                    process.username,
                    process.name,
                    process.status,
                    f"{process.cpu_percent:6.1f}",
                    f"{process.memory_percent:6.1f}",
                    key=str(process.pid),
                )
            if processes:
                table.move_cursor(row=min(old_cursor, len(processes) - 1))
            search_status = f" | filter: {self.search_text!r}" if self.search_text else ""
            self.set_status(
                f"{len(processes)} processes | source: {snapshot.source} | "
                f"sort: {self.sort_mode.value}{search_status}"
            )
        except Exception as error:  # Keep the monitor alive and report transient errors.
            self.set_status(f"Refresh error: {error}")
        finally:
            self.refreshing = False

    def set_status(self, message: str) -> None:
        self.query_one("#status", Static).update(message)

    def selected_pid(self) -> int | None:
        table = self.query_one("#process-table", DataTable)
        if table.row_count == 0:
            return None
        try:
            return int(table.get_row_at(table.cursor_row)[0])
        except (IndexError, ValueError):
            return None

    def action_search(self) -> None:
        search = self.query_one("#search", Input)
        self._opening_search = True
        search.display = True
        search.value = self.search_text
        search.focus()

    def on_input_changed(self, event: Input.Changed) -> None:
        if event.input.id == "search":
            if self._opening_search and event.value == f"{self.search_text}/":
                self._opening_search = False
                event.input.value = self.search_text
                return
            self._opening_search = False
            self.search_text = event.value

    def on_input_submitted(self, event: Input.Submitted) -> None:
        if event.input.id == "search":
            event.input.display = False
            self.query_one("#process-table", DataTable).focus()

    async def set_sort(self, mode: SortMode) -> None:
        self.sort_mode = mode
        await self.update_monitor()

    async def action_sort_cpu(self) -> None:
        await self.set_sort(SortMode.CPU)

    async def action_sort_memory(self) -> None:
        await self.set_sort(SortMode.MEMORY)

    async def action_sort_pid(self) -> None:
        await self.set_sort(SortMode.PID)

    async def action_sort_name(self) -> None:
        await self.set_sort(SortMode.NAME)

    async def action_pause(self) -> None:
        self.paused = not self.paused
        self.set_status("Paused — press Space to resume" if self.paused else "Resumed")
        if not self.paused:
            await self.update_monitor()

    async def action_refresh_now(self) -> None:
        await self.update_monitor()

    def action_terminate(self) -> None:
        self.confirm_signal(force=False)

    def action_kill(self) -> None:
        self.confirm_signal(force=True)

    def confirm_signal(self, force: bool) -> None:
        pid = self.selected_pid()
        if pid is None:
            self.set_status("Select a process first")
            return
        self.push_screen(
            ConfirmSignalScreen(pid, force),
            lambda confirmed: self.execute_signal(pid, force) if confirmed else None,
        )

    def execute_signal(self, pid: int, force: bool) -> None:
        try:
            message = self.process_monitor.send_signal(pid, force)
        except (ValueError, psutil.Error, PermissionError, ProcessLookupError) as error:
            message = f"Signal failed for PID {pid}: {error}"
        self.set_status(message)
        self.set_timer(0.5, self.update_monitor)
