#!/usr/bin/env python3
"""Back up committed heater RAM records over SWD without halting the CPU."""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from datetime import datetime
import io
import math
import os
from pathlib import Path
import re
import socket
import struct
import subprocess
import sys
import tempfile
import time

from capture_ram_log import _gdb_quote, capacity_warning
from extract_ram_log import CSV_COLUMNS, CSV_COLUMNS_V2, HEADER, MAGIC, SAMPLE, decode


class CaptureError(RuntimeError):
    pass


class SessionChanged(CaptureError):
    pass


@dataclass(frozen=True)
class Layout:
    address: int
    size: int
    start_tick_address: int

    def validate(self):
        for address, size in ((self.address, self.size), (self.start_tick_address, 4)):
            if address % 4 or size <= 0 or not 0x20000000 <= address < address+size <= 0x20020000:
                raise CaptureError('Symbol ELF không nằm trong SRAM STM32F407VG dự kiến; kiểm tra đúng ELF.')
        if self.size < HEADER.size:
            raise CaptureError('Kích thước log ELF không hợp lệ.')


def symbol_commands(elf):
    # GDB reads the local ELF only. It never connects to or controls the board.
    return f'''set pagination off
set confirm off
set max-value-size unlimited
file {_gdb_quote(elf)}
printf "HEATER_LIVE_LAYOUT %lu %lu %lu\\n", (unsigned long)&gHeaterTestLog, (unsigned long)sizeof(gHeaterTestLog), (unsigned long)&heaterTestStartTick
quit
'''


def resolve_layout(elf, gdb):
    with tempfile.TemporaryDirectory(prefix='heater-symbols-') as temporary:
        command_file = Path(temporary)/'symbols.gdb'
        command_file.write_text(symbol_commands(elf.resolve()), encoding='utf-8')
        executable = str(Path(gdb).resolve()) if Path(gdb).is_file() else gdb
        result = subprocess.run([executable, '--batch', '--nx', '-x', 'symbols.gdb'],
                                cwd=temporary, capture_output=True, text=True, timeout=30)
    match = re.search(r'^HEATER_LIVE_LAYOUT (\d+) (\d+) (\d+)\s*$', result.stdout, re.MULTILINE)
    if result.returncode or not match:
        raise CaptureError('Không đọc được symbol gHeaterTestLog/ heaterTestStartTick. '
                           'Dùng ELF Debug đúng với firmware đã nạp. '+result.stderr.strip())
    layout = Layout(*(int(value) for value in match.groups()))
    layout.validate()
    return layout


def endpoint(value):
    host, separator, port = value.rpartition(':')
    if not separator or not host or not port.isdecimal() or not 1 <= int(port) <= 65535:
        raise ValueError('Địa chỉ OpenOCD TCL phải có dạng localhost:6666.')
    return host, int(port)


class OpenOcdRpc:
    """Read-only OpenOCD TCL RPC, with SUB-delimited replies."""
    def __init__(self, connection):
        self.connection = connection
        self.pending = b''

    def request(self, command):
        # Restrict this transport to the two read-only operations used below.
        if command != '[target current] curstate' and not re.fullmatch(
                r'read_memory 0x[0-9a-f]+ (8|32) [1-9][0-9]*', command):
            raise CaptureError('Từ chối lệnh ngoài chế độ chỉ đọc.')
        self.connection.sendall(command.encode('ascii')+b'\x1a')
        while b'\x1a' not in self.pending:
            data = self.connection.recv(65536)
            if not data:
                raise CaptureError('Mất kết nối OpenOCD TCL; các CSV đã lưu vẫn được giữ.')
            self.pending += data
            if len(self.pending) > 131072:
                raise CaptureError('Phản hồi OpenOCD TCL quá lớn hoặc thiếu dấu kết thúc.')
        result, self.pending = self.pending.split(b'\x1a', 1)
        return result.decode('utf-8').strip()

    def assert_running(self):
        state = self.request('[target current] curstate')
        if state != 'running':
            raise CaptureError(f'MCU đang ở trạng thái {state!r}. Công cụ không tự halt/resume/reset. '
                               'Kiểm tra bo và đóng phiên Debug trước khi tiếp tục.')

    def read(self, address, size):
        blocks = []
        for offset in range(0, size, 1024):
            count = min(1024, size-offset)
            # Aligned word transfers keep sampleCount/startTick reads atomic on
            # Cortex-M4 and avoid tearing a counter at a byte carry boundary.
            width = 32 if (address+offset) % 4 == 0 and count % 4 == 0 else 8
            units = count//4 if width == 32 else count
            reply = self.request(f'read_memory 0x{address+offset:x} {width} {units}')
            try:
                words = reply.split()
                values = [int(word, 0) for word in words]
                data = struct.pack('<'+'I'*len(values), *values) if width == 32 else bytes(values)
            except (ValueError, OverflowError, struct.error) as exc:
                raise CaptureError('OpenOCD không đọc được RAM khi CPU chạy: '+reply[:200]) from exc
            if len(data) != count:
                raise CaptureError(f'Đọc RAM thiếu byte ({len(data)}/{count}); không lưu bản thiếu.')
            blocks.append(data)
        return b''.join(blocks)


@dataclass(frozen=True)
class HeaderState:
    raw: bytes
    version: int
    sample_size: int
    capacity: int
    count: int
    complete: bool

    @property
    def format(self):
        return self.version, self.sample_size, self.capacity


def parse_header(raw, size):
    if raw == bytes(HEADER.size):
        return None  # BSS before the first START.
    if len(raw) != HEADER.size:
        raise CaptureError('Thiếu header RAM log.')
    magic, version, sample_size, capacity, count, complete = HEADER.unpack(raw)
    if (magic != MAGIC or version not in (1, 2) or sample_size != SAMPLE.size
            or capacity == 0 or count > capacity or complete not in (0, 1)
            or HEADER.size+capacity*sample_size != size):
        raise CaptureError('Header RAM không khớp ELF/định dạng log; không ghi đè CSV đã lưu.')
    return HeaderState(raw, version, sample_size, capacity, count, bool(complete))


@dataclass(frozen=True)
class Snapshot:
    payload: bytes
    rows: list
    complete: bool
    capacity: int
    start_tick: int


class LiveReader:
    def __init__(self, rpc, layout):
        layout.validate()
        self.rpc, self.layout = rpc, layout
        self.start_tick = None
        self.seen_count = 0
        self.format = None
        self.seen_complete = False

    def poll(self):
        self.rpc.assert_running()
        for _ in range(3):
            start = struct.unpack('<I', self.rpc.read(self.layout.start_tick_address, 4))[0]
            raw = self.rpc.read(self.layout.address, HEADER.size)
            finish = struct.unpack('<I', self.rpc.read(self.layout.start_tick_address, 4))[0]
            if start != finish:
                if self.start_tick is not None:
                    raise SessionChanged('Phát hiện START/reset sang lần mới; giữ các CSV đã lưu của lần cũ.')
                continue
            try:
                header = parse_header(raw, self.layout.size)
            except CaptureError as exc:
                if self.start_tick is not None:
                    raise SessionChanged('Log bị reset/thay đổi trong lúc thu; giữ các CSV đã lưu.') from exc
                raise
            if header is None or header.count == 0:
                if self.start_tick is not None:
                    raise SessionChanged('Log đã bị xóa; giữ các CSV đã lưu, không trộn hai lần đo.')
                return None
            if self.start_tick is not None and (start != self.start_tick or header.count < self.seen_count
                                                or header.format != self.format
                                                or (self.seen_complete and not header.complete)):
                raise SessionChanged('Log chuyển sang lần khác hoặc số mẫu giảm; không ghi đè dữ liệu cũ.')
            self.start_tick, self.seen_count, self.format = start, header.count, header.format
            self.seen_complete = header.complete
            return header
        raise CaptureError('Không đọc được mốc START ổn định; thử lại khi log đã bắt đầu.')

    def snapshot(self):
        for _ in range(3):
            before = self.poll()
            if before is None:
                return None
            records = self.rpc.read(self.layout.address+HEADER.size, before.count*before.sample_size)
            after = self.poll()
            if after is None:
                raise SessionChanged('Log biến mất trong khi đọc.')
            if after.complete and (not before.complete or after.count != before.count):
                continue  # Re-read to include the final record and complete flag together.
            payload = before.raw+records
            rows, complete, _, capacity = decode(payload)
            if rows[0][0] != 0 or any(b[0] < a[0] for a, b in zip(rows, rows[1:])):
                raise CaptureError('Mốc thời gian log không hợp lệ; không lưu bản đọc không nhất quán.')
            return Snapshot(payload, rows, complete, capacity, self.start_tick)
        raise CaptureError('Log đổi trạng thái liên tục trong khi đọc; giữ các CSV trước đó.')


def atomic_write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix='.'+path.name+'.',
                                         suffix='.tmp', delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def save_snapshot(snapshot, csv_path, backup_dir, dump_path=None):
    stream = io.StringIO(newline='')
    writer = csv.writer(stream, lineterminator='\n')
    writer.writerow(CSV_COLUMNS_V2 if len(snapshot.rows[0]) == 7 else CSV_COLUMNS)
    writer.writerows(snapshot.rows)
    data = stream.getvalue().encode('utf-8')
    stamp = datetime.now().strftime('%Y%m%dT%H%M%S%f')
    elapsed_ms = snapshot.rows[-1][0]
    state = 'final' if snapshot.complete else 'partial'
    backup = backup_dir/f'{csv_path.stem}_{stamp}_{elapsed_ms:010d}ms_{state}.csv'
    atomic_write(backup, data)  # Keep a separate backup even if Excel locks the latest CSV.
    try:
        atomic_write(csv_path, data)
    except PermissionError as exc:
        print(f'LƯU Ý: {csv_path} đang bị khóa; đã lưu bản riêng {backup}. Đóng Excel để cập nhật file chính.', file=sys.stderr)
        if snapshot.complete:
            raise CaptureError(f'Bản cuối đã lưu ở {backup}, nhưng CSV chính đang bị khóa. '
                               'Đóng Excel trước khi lấy lại log hoàn tất.') from exc
    if dump_path is not None:
        atomic_write(dump_path, snapshot.payload)
    last = snapshot.rows[-1]
    power = f'u={last[5]}' if len(last) == 7 else f'{last[1]}%'
    status = f'HOÀN TẤT ({last[4]})' if snapshot.complete else 'ĐÃ LƯU'
    print(f'[{datetime.now():%H:%M:%S}] {status} | {elapsed_ms/60000:.1f} phút | '
          f'{power} | {last[3]:.1f} °C | {len(snapshot.rows)} mẫu', flush=True)
    return backup


def collect(reader, csv_path, backup_dir, dump_path=None, *, autosave_seconds=600,
            poll_seconds=5, clock=time.monotonic, sleep=time.sleep):
    next_save = None
    while True:
        header = reader.poll()
        now = clock()
        if header is not None and (next_save is None or now >= next_save or header.complete):
            snapshot = reader.snapshot()
            if snapshot is not None:
                save_snapshot(snapshot, csv_path, backup_dir, dump_path)
                next_save = clock()+autosave_seconds
                if snapshot.complete:
                    return snapshot
        sleep(poll_seconds)


def connect_rpc(target, process=None, timeout=10):
    address = endpoint(target)
    deadline = time.monotonic()+timeout
    while True:
        if process is not None and process.poll() is not None:
            raise CaptureError(f'OpenOCD đã dừng với mã {process.returncode}.')
        try:
            connection = socket.create_connection(address, timeout=1)
            connection.settimeout(5)
            return connection
        except OSError:
            if time.monotonic() >= deadline:
                raise CaptureError(f'Không kết nối được cổng TCL {target}.')
            time.sleep(.1)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', required=True, type=Path)
    parser.add_argument('--csv', required=True, type=Path)
    parser.add_argument('--gdb', default='arm-none-eabi-gdb', help='chỉ đọc symbol ELF offline')
    parser.add_argument('--tcl-target', default='localhost:6666')
    parser.add_argument('--start-openocd', action='store_true')
    parser.add_argument('--openocd', default='openocd')
    parser.add_argument('--openocd-log', type=Path,
                        help='log kỹ thuật OpenOCD; mặc định openocd.log cạnh CSV khi tự mở server')
    parser.add_argument('--keep-dump', type=Path)
    parser.add_argument('--backup-dir', type=Path)
    parser.add_argument('--autosave-seconds', type=float, default=600)
    parser.add_argument('--poll-seconds', type=float, default=5)
    args = parser.parse_args(argv)
    if not args.elf.is_file():
        parser.error(f'Không tìm thấy ELF: {args.elf}')
    if args.csv.exists():
        parser.error('CSV đích đã tồn tại. Chọn file/thư mục lần đo mới để giữ nguyên dữ liệu trước.')
    if args.keep_dump and args.keep_dump.resolve() == args.csv.resolve():
        parser.error('CSV và dump RAM phải là hai file khác nhau.')
    log_path = args.openocd_log or args.csv.parent/'openocd.log'
    if args.start_openocd and (log_path.resolve() == args.csv.resolve()
                              or (args.keep_dump and log_path.resolve() == args.keep_dump.resolve())):
        parser.error('Log OpenOCD phải khác file CSV và dump RAM.')
    if any(not math.isfinite(value) or value <= 0 for value in (args.autosave_seconds, args.poll_seconds)):
        parser.error('Chu kỳ lưu và kiểm tra phải hữu hạn và lớn hơn 0.')
    try:
        host, port = endpoint(args.tcl_target)
    except ValueError as exc:
        parser.error(str(exc))
    if args.start_openocd and host not in ('localhost', '127.0.0.1'):
        parser.error('--start-openocd chỉ dùng cổng TCL trên máy này.')
    process = None
    openocd_output = None
    try:
        layout = resolve_layout(args.elf, args.gdb)
        if args.start_openocd:
            log_path.parent.mkdir(parents=True, exist_ok=True)
            openocd_output = log_path.open('a', encoding='utf-8')
            openocd_output.write(f'\n=== {datetime.now():%Y-%m-%d %H:%M:%S} OpenOCD ===\n')
            openocd_output.flush()
            process = subprocess.Popen([args.openocd, '-f', 'interface/stlink.cfg',
                                        '-f', 'target/stm32f4x.cfg', '-c', f'tcl_port {port}'],
                                       stdin=subprocess.DEVNULL, stdout=openocd_output,
                                       stderr=subprocess.STDOUT)
        with connect_rpc(args.tcl_target, process) as connection:
            rpc = OpenOcdRpc(connection)
            reader = LiveReader(rpc, layout)
            reader.poll()  # Probe background memory access before inviting START.
            print(f'Đã kết nối SWD | Tự lưu CSV mỗi {args.autosave_seconds/60:g} phút', flush=True)
            print(f'CSV: {args.csv}\nBản sao: {args.backup_dir or args.csv.parent/"snapshots"}', flush=True)
            if openocd_output is not None:
                print(f'Log kỹ thuật: {log_path}', flush=True)
            print('Chờ START trên nồi; nếu đã chạy thì tiếp tục thu lần hiện tại.', flush=True)
            result = collect(reader, args.csv,
                             args.backup_dir or args.csv.parent/'snapshots', args.keep_dump,
                             autosave_seconds=args.autosave_seconds, poll_seconds=args.poll_seconds)
            warning = capacity_warning(result.capacity)
            if warning:
                print(warning, file=sys.stderr)
        print('Đã nhận log hoàn tất. Có thể phân tích/ghép đồ thị từ CSV chính.')
        return 0
    except KeyboardInterrupt:
        print('\nĐã dừng thu trên PC; nồi có thể vẫn đang gia nhiệt. '
              'Nhấn START trên nồi để dừng nếu cần. Các CSV đã lưu được giữ nguyên.', file=sys.stderr)
        return 130
    except (CaptureError, OSError, ValueError, subprocess.TimeoutExpired) as exc:
        print(f'LỖI: {exc}\nGiữ nguyên các CSV đã lưu. Công cụ không gửi lệnh dừng nồi, halt hoặc reset.', file=sys.stderr)
        if openocd_output is not None:
            print(f'Chi tiết OpenOCD: {log_path}', file=sys.stderr)
        return 2
    finally:
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        if openocd_output is not None:
            openocd_output.close()


if __name__ == '__main__':
    raise SystemExit(main())
