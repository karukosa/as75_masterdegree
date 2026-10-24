#!/usr/bin/env python3
"""Convert an STM32 SRAM binary dump containing gHeaterTestLog to CSV."""

import argparse
import csv
import struct
from pathlib import Path

MAGIC = b"HTLOG001"
HEADER = struct.Struct("<8sHHIIB3x")
SAMPLE = struct.Struct("<IhBBB3x")
SAMPLE_V2 = struct.Struct("<IhBBBBBB")
CSV_COLUMNS = ("elapsed_ms", "power_percent", "heater_on", "temperature_c", "status")
CSV_COLUMNS_V2 = CSV_COLUMNS + ("control_u", "control_u_max")
STATUS = {
    0: "RUN",
    1: "DONE",
    2: "STOP",
    3: "SENSOR_ERROR",
    4: "OVER_TEMP",
    5: "DOOR_ERROR",
    6: "BUFFER_FULL",
    7: "POWER_CHANGE",
}


def decode(data: bytes):
    offset = data.find(MAGIC)
    if offset < 0:
        raise ValueError("không tìm thấy HTLOG001 trong file dump")
    if len(data) < offset + HEADER.size:
        raise ValueError("header RAM log bị thiếu")

    magic, version, sample_size, capacity, count, complete = HEADER.unpack_from(data, offset)
    if magic != MAGIC or version not in (1, 2) or sample_size != SAMPLE.size:
        raise ValueError(
            f"định dạng không hỗ trợ: version={version}, sample_size={sample_size}"
        )
    if count > capacity:
        raise ValueError(f"sample_count {count} lớn hơn capacity {capacity}")
    end = offset + HEADER.size + count * sample_size
    if end > len(data):
        raise ValueError("file dump không chứa đủ số mẫu được khai báo")

    rows = []
    position = offset + HEADER.size
    for _ in range(count):
        elapsed, temperature, power, heater, status = SAMPLE.unpack_from(data, position)
        rows.append((elapsed, power, heater, temperature / 10.0, STATUS.get(status, f"UNKNOWN_{status}")))
        if version == 2:
            _, _, _, _, _, control_u, control_max, _ = SAMPLE_V2.unpack_from(data, position)
            if control_max == 0 or control_u > control_max:
                raise ValueError("giá trị u hoặc thang u trong log không hợp lệ")
            rows[-1] += (control_u, control_max)
        position += sample_size
    return rows, bool(complete), offset, capacity


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dump", type=Path, help="file nhị phân dump từ SRAM")
    parser.add_argument("--csv", type=Path, required=True, help="file CSV đầu ra")
    args = parser.parse_args()

    try:
        rows, complete, offset, capacity = decode(args.dump.read_bytes())
    except (OSError, ValueError) as exc:
        parser.error(str(exc))

    args.csv.parent.mkdir(parents=True, exist_ok=True)
    with args.csv.open("w", newline="", encoding="utf-8") as output:
        writer = csv.writer(output, lineterminator="\n")
        writer.writerow(CSV_COLUMNS_V2 if rows and len(rows[0]) == 7 else CSV_COLUMNS)
        writer.writerows(rows)

    state = "hoàn tất" if complete else "đang ghi/chưa hoàn tất"
    print(f"Đã xuất {len(rows)}/{capacity} mẫu ({state}), log tại offset 0x{offset:x}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
