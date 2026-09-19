import unittest

from monitor.procfs import ProcfsReader


class ProcfsReaderTests(unittest.TestCase):
    def test_parse_cpu_stat(self) -> None:
        parsed = ProcfsReader.parse_cpu_stat(
            "cpu  10 2 3 85 1 0 0 0 0 0\n"
            "cpu0 5 1 2 42 0 0 0 0 0 0\n"
            "intr 123\n"
        )
        self.assertEqual(parsed["cpu"], (101, 86))
        self.assertEqual(parsed["cpu0"], (50, 42))

    def test_parse_meminfo_converts_kib_to_bytes(self) -> None:
        parsed = ProcfsReader.parse_meminfo(
            "MemTotal: 1024 kB\nMemAvailable: 256 kB\n"
        )
        self.assertEqual(parsed["MemTotal"], 1024 * 1024)
        self.assertEqual(parsed["MemAvailable"], 256 * 1024)

    def test_cpu_percent_uses_two_samples(self) -> None:
        # Total increases by 100 ticks and idle by 25, so usage is 75%.
        self.assertEqual(ProcfsReader._percent((100, 50), (200, 75)), 75.0)


if __name__ == "__main__":
    unittest.main()
