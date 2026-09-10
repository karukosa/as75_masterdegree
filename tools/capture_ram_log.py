#!/usr/bin/env python3
"""Wait for a heater test over SWD, then dump its RAM log and create CSV."""

import argparse
import csv
import shutil
import socket
import subprocess
import tempfile
import time
from pathlib import Path

from extract_ram_log import decode


def _gdb_quote(path: Path) -> str:
    # GDB accepts forward slashes on Windows.  Using them avoids GDB treating
    # backslashes in a quoted filename as escape characters (for example,
    # ``\\t`` in a directory name), which can make an existing path look absent.
    return '"' + str(path).replace("\\", "/").replace('"', '\\"') + '"'


def build_gdb_commands(elf: Path, dump: Path, target: str) -> str:
    """Build a batch GDB program which waits for complete to change to one."""
    return f"""set pagination off
set confirm off
set remotetimeout 5
file {_gdb_quote(elf)}
target extended-remote {target}
monitor halt
watch -l gHeaterTestLog.complete
commands
  silent
  if gHeaterTestLog.complete == 1
    dump binary value {_gdb_quote(dump)} gHeaterTestLog
    printf "\\nDa nhan du log RAM.\\n"
    detach
    quit
  end
  continue
end
printf "Da ket noi SWD. Hay nhan START tren may.\\n"
continue
"""


def write_csv(path: Path, rows) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.writer(output, lineterminator="\n")
        writer.writerow(("elapsed_ms", "power_percent", "heater_on", "temperature_c", "status"))
        writer.writerows(rows)


def wait_for_gdb_server(target: str, process, timeout: float = 10.0) -> None:
    host, separator, port_text = target.rpartition(":")
    if not separator or not host:
        raise ValueError("--target phải có dạng host:port")
    port = int(port_text)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"OpenOCD đã dừng với mã lỗi {process.returncode}")
        try:
            with socket.create_connection((host, port), timeout=0.25):
                return
        except OSError:
            time.sleep(0.1)
    raise TimeoutError(f"OpenOCD không mở {target} sau {timeout:g} giây")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", required=True, type=Path, help="firmware ELF có symbol debug")
    parser.add_argument("--csv", required=True, type=Path, help="file CSV đầu ra")
    parser.add_argument("--gdb", default="arm-none-eabi-gdb", help="đường dẫn GDB")
    parser.add_argument("--target", default="localhost:3333", help="GDB server của OpenOCD")
    parser.add_argument("--keep-dump", type=Path, help="giữ thêm file dump nhị phân")
    parser.add_argument(
        "--start-openocd", action="store_true",
        help="tự mở OpenOCD cho ST-Link và STM32F4, rồi đóng khi xong",
    )
    parser.add_argument("--openocd", default="openocd", help="đường dẫn OpenOCD")
    args = parser.parse_args()

    if not args.elf.is_file():
        parser.error(f"không tìm thấy ELF: {args.elf}")

    openocd_process = None
    try:
        if args.start_openocd:
            try:
                openocd_process = subprocess.Popen([
                    args.openocd, "-f", "interface/stlink.cfg",
                    "-f", "target/stm32f4x.cfg",
                ])
                wait_for_gdb_server(args.target, openocd_process)
            except FileNotFoundError:
                parser.error(f"không tìm thấy OpenOCD: {args.openocd}")
            except (ValueError, RuntimeError, TimeoutError) as exc:
                parser.error(str(exc))

        with tempfile.TemporaryDirectory(prefix="heater-log-") as temporary:
            temporary_path = Path(temporary)
            # Let GDB write a simple relative name inside an existing temporary
            # directory. Some Windows GDB builds reject even valid absolute paths.
            dump_name = Path("heater_ram.bin")
            dump = temporary_path / dump_name
            command_file = temporary_path / "capture.gdb"
            command_file.write_text(
                build_gdb_commands(args.elf.resolve(), dump_name, args.target), encoding="utf-8"
            )
            gdb = Path(args.gdb)
            gdb_command = str(gdb.resolve()) if gdb.is_file() else args.gdb
            try:
                subprocess.run(
                    [gdb_command, "--batch", "-x", "capture.gdb"],
                    check=True,
                    cwd=temporary_path,
                )
            except FileNotFoundError:
                parser.error(f"không tìm thấy GDB: {args.gdb}")
            except subprocess.CalledProcessError as exc:
                parser.error(f"GDB kết thúc với mã lỗi {exc.returncode}")

            if args.keep_dump:
                kept_dump = args.keep_dump.resolve()
                kept_dump.parent.mkdir(parents=True, exist_ok=True)
                try:
                    shutil.copyfile(dump, kept_dump)
                except OSError as exc:
                    parser.error(f"không thể lưu file dump: {exc}")

            try:
                rows, complete, _, capacity = decode(dump.read_bytes())
            except (OSError, ValueError) as exc:
                parser.error(str(exc))
            if not complete:
                parser.error("log nhận được chưa hoàn tất")
            write_csv(args.csv, rows)
    finally:
        if openocd_process is not None and openocd_process.poll() is None:
            openocd_process.terminate()
            try:
                openocd_process.wait(timeout=3.0)
            except subprocess.TimeoutExpired:
                openocd_process.kill()
                openocd_process.wait()

    print(f"Đã tự động xuất {len(rows)}/{capacity} bản ghi vào {args.csv}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
