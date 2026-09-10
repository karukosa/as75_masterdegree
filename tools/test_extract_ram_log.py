import csv
import struct
import tempfile
import unittest
from pathlib import Path, PureWindowsPath
from unittest import mock

import extract_ram_log
import capture_ram_log


class ExtractRamLogTest(unittest.TestCase):
    def test_decode_and_csv_values(self):
        header = extract_ram_log.HEADER.pack(
            extract_ram_log.MAGIC, 1, extract_ram_log.SAMPLE.size, 151, 2, 1
        )
        samples = b"".join(
            [
                extract_ram_log.SAMPLE.pack(0, 253, 70, 1, 0),
                extract_ram_log.SAMPLE.pack(1000, -15, 70, 0, 1),
            ]
        )
        rows, complete, offset, capacity = extract_ram_log.decode(b"prefix" + header + samples)
        self.assertEqual(offset, 6)
        self.assertTrue(complete)
        self.assertEqual(capacity, 151)
        self.assertEqual(rows[0], (0, 70, 1, 25.3, "RUN"))
        self.assertEqual(rows[1], (1000, 70, 0, -1.5, "DONE"))

    def test_rejects_missing_magic(self):
        with self.assertRaisesRegex(ValueError, "HTLOG001"):
            extract_ram_log.decode(bytes(64))

    def test_capture_waits_for_complete_then_dumps(self):
        commands = capture_ram_log.build_gdb_commands(
            Path("firmware.elf"), Path("heater.bin"), "localhost:3333"
        )
        self.assertIn("watch -l gHeaterTestLog.complete", commands)
        self.assertIn("set remotetimeout 5", commands)
        self.assertIn("if gHeaterTestLog.complete == 1", commands)
        self.assertIn("dump binary value heater.bin gHeaterTestLog", commands)

    def test_capture_warns_when_old_25_minute_firmware_is_connected(self):
        warning = capture_ram_log.capacity_warning(151)

        self.assertIn("151", warning)
        self.assertIn("211", warning)
        self.assertIn("Clean/Rebuild", warning)

    def test_capture_accepts_35_minute_log_capacity(self):
        self.assertIsNone(capture_ram_log.capacity_warning(211))

    def test_capture_uses_forward_slashes_for_windows_elf_path(self):
        commands = capture_ram_log.build_gdb_commands(
            PureWindowsPath(r"C:\Users\Admin\firmware.elf"),
            Path("heater_ram.bin"),
            "localhost:3333",
        )

        self.assertIn('file "C:/Users/Admin/firmware.elf"', commands)
        self.assertIn("dump binary value heater_ram.bin", commands)
        self.assertNotIn(r"C:\\Users", commands)

    def test_capture_rejects_unsafe_internal_dump_name(self):
        with self.assertRaisesRegex(ValueError, "khoảng trắng"):
            capture_ram_log.build_gdb_commands(
                Path("firmware.elf"), Path("heater ram.bin"), "localhost:3333"
            )

    def test_write_csv_creates_parent_directories(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "new" / "data" / "step_100.csv"

            capture_ram_log.write_csv(output, [(0, 100, 1, 25.0, "STOP")])

            with output.open(newline="", encoding="utf-8") as csv_file:
                self.assertEqual(
                    list(csv.reader(csv_file)),
                    [
                        ["elapsed_ms", "power_percent", "heater_on", "temperature_c", "status"],
                        ["0", "100", "1", "25.0", "STOP"],
                    ],
                )

    def test_capture_dumps_to_temporary_working_directory(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            elf = root / "firmware.elf"
            elf.write_bytes(b"ELF")
            kept_dump = root / "missing" / "data" / "heater_ram_70.bin"
            csv_path = root / "data" / "step_70.csv"
            ram = (
                extract_ram_log.HEADER.pack(
                    extract_ram_log.MAGIC, 1, extract_ram_log.SAMPLE.size, 151, 0, 1
                )
            )

            def fake_run(command, check, cwd):
                self.assertEqual(command[-2:], ["-x", "capture.gdb"])
                self.assertTrue(check)
                self.assertTrue((cwd / "heater_ram.bin").is_file())
                self.assertEqual((cwd / "heater_ram.bin").stat().st_size, 0)
                gdb_commands = (cwd / "capture.gdb").read_text(encoding="utf-8")
                self.assertIn("dump binary value heater_ram.bin", gdb_commands)
                self.assertNotIn(str(kept_dump), gdb_commands)
                (cwd / "heater_ram.bin").write_bytes(ram)

            argv = [
                "capture_ram_log.py", "--elf", str(elf), "--csv", str(csv_path),
                "--keep-dump", str(kept_dump),
            ]
            with mock.patch.object(capture_ram_log.subprocess, "run", side_effect=fake_run), \
                    mock.patch("sys.argv", argv):
                self.assertEqual(capture_ram_log.main(), 0)

            self.assertEqual(kept_dump.read_bytes(), ram)
            self.assertTrue(csv_path.is_file())

    def test_capture_rejects_an_empty_gdb_dump(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            elf = root / "firmware.elf"
            elf.write_bytes(b"ELF")
            argv = [
                "capture_ram_log.py", "--elf", str(elf),
                "--csv", str(root / "step.csv"),
            ]

            with mock.patch.object(capture_ram_log.subprocess, "run"), \
                    mock.patch("sys.argv", argv), \
                    self.assertRaisesRegex(SystemExit, "2"):
                capture_ram_log.main()


if __name__ == "__main__":
    unittest.main()
