#!/usr/bin/env python3
"""Wait for a heater test over SWD, then dump its RAM log and create CSV."""

import argparse
import csv
import subprocess
import tempfile
from pathlib import Path

from extract_ram_log import decode


def _gdb_quote(path: Path) -> str:
    return '"' + str(path).replace("\\", "\\\\").replace('"', '\\"') + '"'


def build_gdb_commands(elf: Path, dump: Path, target: str) -> str:
    """Build a batch GDB program which waits for complete to change to one."""
    return f"""set pagination off
set confirm off
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
    with path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.writer(output, lineterminator="\n")
        writer.writerow(("elapsed_ms", "power_percent", "heater_on", "temperature_c", "status"))
        writer.writerows(rows)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", required=True, type=Path, help="firmware ELF có symbol debug")
    parser.add_argument("--csv", required=True, type=Path, help="file CSV đầu ra")
    parser.add_argument("--gdb", default="arm-none-eabi-gdb", help="đường dẫn GDB")
    parser.add_argument("--target", default="localhost:3333", help="GDB server của OpenOCD")
    parser.add_argument("--keep-dump", type=Path, help="giữ thêm file dump nhị phân")
    args = parser.parse_args()

    if not args.elf.is_file():
        parser.error(f"không tìm thấy ELF: {args.elf}")

    with tempfile.TemporaryDirectory(prefix="heater-log-") as temporary:
        temporary_path = Path(temporary)
        dump = args.keep_dump.resolve() if args.keep_dump else temporary_path / "heater_ram.bin"
        command_file = temporary_path / "capture.gdb"
        command_file.write_text(
            build_gdb_commands(args.elf.resolve(), dump, args.target), encoding="utf-8"
        )
        try:
            subprocess.run([args.gdb, "--batch", "-x", str(command_file)], check=True)
        except FileNotFoundError:
            parser.error(f"không tìm thấy GDB: {args.gdb}")
        except subprocess.CalledProcessError as exc:
            parser.error(f"GDB kết thúc với mã lỗi {exc.returncode}")

        try:
            rows, complete, _, capacity = decode(dump.read_bytes())
        except (OSError, ValueError) as exc:
            parser.error(str(exc))
        if not complete:
            parser.error("log nhận được chưa hoàn tất")
        write_csv(args.csv, rows)

    print(f"Đã tự động xuất {len(rows)}/{capacity} bản ghi vào {args.csv}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
