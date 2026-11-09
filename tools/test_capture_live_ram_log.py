import contextlib
import csv
import io
from pathlib import Path, PureWindowsPath
import re
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

import capture_live_ram_log as live
from extract_ram_log import HEADER, MAGIC, SAMPLE_V2, decode


def record(ms, status=0, temperature=300):
    return SAMPLE_V2.pack(ms, temperature, 30, int(status == 0), status, 77, 255, 0)


class MemoryRpc:
    def __init__(self):
        self.layout = live.Layout(0x20000000, HEADER.size+12*100, 0x2001f000)
        self.state = 'running'
        self.hook = None
        self.set([])

    def set(self, rows, complete=False, tick=1234):
        self.tick = tick
        self.payload = HEADER.pack(MAGIC, 2, 12, 100, len(rows), int(complete))+b''.join(rows)
        self.payload += bytes(self.layout.size-len(self.payload))

    def assert_running(self):
        if self.state != 'running':
            raise live.CaptureError('MCU is not running')

    def read(self, address, size):
        if self.hook:
            self.hook(address, size)
        if address == self.layout.start_tick_address:
            return struct.pack('<I', self.tick)
        offset = address-self.layout.address
        return self.payload[offset:offset+size]


class FramedSocket:
    def __init__(self, data):
        self.data = data
        self.sent = []

    def sendall(self, data):
        self.sent.append(data)

    def recv(self, count):
        chunk, self.data = self.data[:3], self.data[3:]
        return chunk


class LiveCaptureTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def test_symbol_lookup_is_offline_and_uses_correct_windows_quoting(self):
        text = live.symbol_commands(PureWindowsPath(r'C:\My Project\Debug\firmware.elf'))
        self.assertIn('file "C:/My Project/Debug/firmware.elf"', text)
        self.assertIn('&heaterTestStartTick', text)
        for command in ('target ', 'monitor ', 'continue', 'halt', 'reset', 'resume'):
            self.assertNotIn(command, text)
        reply = SimpleNamespace(returncode=0, stdout=f'HEATER_LIVE_LAYOUT {0x20000000} 86472 {0x2001f000}\n', stderr='')
        with mock.patch.object(live.subprocess, 'run', return_value=reply) as run:
            layout = live.resolve_layout(self.root/'firmware.elf', 'arm-none-eabi-gdb')
        self.assertEqual(layout.size, 86472)
        self.assertIn('--nx', run.call_args.args[0])

    def test_rejects_wrong_symbol_address_or_truncated_elf_output(self):
        with self.assertRaises(live.CaptureError):
            live.Layout(0x08000000, 86472, 0x2001f000).validate()
        reply = SimpleNamespace(returncode=1, stdout='', stderr='No symbol')
        with mock.patch.object(live.subprocess, 'run', return_value=reply):
            with self.assertRaisesRegex(live.CaptureError, 'ELF Debug'):
                live.resolve_layout(self.root/'firmware.elf', 'gdb')

    def test_tcl_frames_and_read_only_allowlist(self):
        connection = FramedSocket(b'running\x1a0x00 0x7f 255\x1a')
        rpc = live.OpenOcdRpc(connection)
        rpc.assert_running()
        self.assertEqual(rpc.read(0x20000000, 3), bytes([0,127,255]))
        self.assertEqual(connection.sent, [b'[target current] curstate\x1a', b'read_memory 0x20000000 8 3\x1a'])
        for command in ('halt', 'resume', 'reset halt', 'mww 0x20000000 0', 'read_memory 0x20000000 8 3; halt'):
            with self.assertRaises(live.CaptureError):
                rpc.request(command)
        self.assertEqual(len(connection.sent), 2)

    def test_halted_cpu_and_failed_memory_read_have_no_halt_fallback(self):
        connection = FramedSocket(b'halted\x1a')
        with self.assertRaises(live.CaptureError):
            live.OpenOcdRpc(connection).assert_running()
        self.assertEqual(len(connection.sent), 1)
        for reply in (b'Target read failed\x1a', b'1 2\x1a', b'999\x1a', b''):
            rpc = live.OpenOcdRpc(FramedSocket(reply))
            with self.assertRaises(live.CaptureError):
                rpc.read(0x20000000, 3)

    def test_aligned_counters_use_atomic_word_reads_in_little_endian_order(self):
        connection=FramedSocket(b'0x11223344 0xaabbccdd\x1a')
        data=live.OpenOcdRpc(connection).read(0x20000000,8)
        self.assertEqual(data,b'\x44\x33\x22\x11\xdd\xcc\xbb\xaa')
        self.assertEqual(connection.sent,[b'read_memory 0x20000000 32 2\x1a'])

    def test_waits_for_start_and_exports_only_committed_records(self):
        rpc = MemoryRpc()
        rpc.payload = bytes(rpc.layout.size)
        reader = live.LiveReader(rpc, rpc.layout)
        self.assertIsNone(reader.poll())
        rpc.set([record(0)])
        snap = reader.snapshot()
        self.assertEqual(len(snap.rows), 1)
        self.assertFalse(snap.complete)
        self.assertEqual(snap.rows[-1][4], 'RUN')
        self.assertEqual(len(snap.payload), HEADER.size+12)

    def test_full_86472_byte_layout_is_read_in_chunks_without_stopping_cpu(self):
        layout=live.Layout(0x20000000,86472,0x2001f000)
        rows=[record(t*10000) for t in range(7202)]+[record(72015000,2)]
        committed=HEADER.pack(MAGIC,2,12,7204,len(rows),1)+b''.join(rows)
        memory=committed+bytes(layout.size-len(committed))
        class MemorySocket:
            def __init__(self):self.pending=b'';self.commands=[]
            def sendall(self,data):
                command=data[:-1].decode('ascii');self.commands.append(command)
                if command=='[target current] curstate':
                    reply='running'
                else:
                    _,address,width,count=command.split()
                    address,width,count=int(address,0),int(width),int(count)
                    length=width//8*count
                    if address==layout.start_tick_address:
                        raw=struct.pack('<I',1234)
                    else:
                        offset=address-layout.address;raw=memory[offset:offset+length]
                    values=struct.unpack('<'+'I'*count,raw) if width==32 else raw
                    reply=' '.join(str(v) for v in values)
                self.pending+=reply.encode('ascii')+b'\x1a'
            def recv(self,count):
                result,self.pending=self.pending[:517],self.pending[517:]
                return result
        connection=MemorySocket()
        snap=live.LiveReader(live.OpenOcdRpc(connection),layout).snapshot()
        self.assertTrue(snap.complete)
        self.assertEqual(len(snap.rows),7203)
        self.assertEqual(snap.payload,committed)
        self.assertEqual(snap.rows[-1][4],'STOP')
        reads=[c for c in connection.commands if c.startswith('read_memory ')]
        self.assertGreater(len(reads),80)
        self.assertTrue(all(int(c.split()[3])<=256 and c.split()[2]=='32' for c in reads))

    def test_append_during_copy_retains_consistent_prefix(self):
        rpc = MemoryRpc();rpc.set([record(0)])
        def append(address, size):
            if address == rpc.layout.address+HEADER.size:
                rpc.hook = None
                rpc.set([record(0),record(10000)])
        rpc.hook = append
        snap = live.LiveReader(rpc,rpc.layout).snapshot()
        self.assertFalse(snap.complete)
        self.assertEqual(len(snap.rows), 1)
        self.assertEqual(decode(snap.payload)[0], snap.rows)

    def test_completion_during_copy_retries_and_includes_actual_stop(self):
        rpc = MemoryRpc();rpc.set([record(0)])
        def complete(address, size):
            if address == rpc.layout.address+HEADER.size:
                rpc.hook = None
                rpc.set([record(0),record(9755,2)],complete=True)
        rpc.hook = complete
        snap = live.LiveReader(rpc,rpc.layout).snapshot()
        self.assertTrue(snap.complete)
        self.assertEqual(len(snap.rows),2)
        self.assertEqual(snap.rows[-1][:5],(9755,30,0,30.0,'STOP'))

    def test_complete_flag_with_a_stale_count_is_reread_before_final_save(self):
        rpc=MemoryRpc();rpc.set([record(0)],complete=True)
        def finish(address,size):
            if address==rpc.layout.address+HEADER.size:
                rpc.hook=None
                rpc.set([record(0),record(10000,2)],complete=True)
        rpc.hook=finish
        snap=live.LiveReader(rpc,rpc.layout).snapshot()
        self.assertTrue(snap.complete)
        self.assertEqual(len(snap.rows),2)
        self.assertEqual(snap.rows[-1][4],'STOP')

    def test_new_start_or_reset_is_rejected_instead_of_mixing_trials(self):
        for action in ('new_tick','cleared','count_decreased','format_changed'):
            rpc = MemoryRpc();rpc.set([record(0),record(10000)])
            reader = live.LiveReader(rpc,rpc.layout);reader.poll()
            if action == 'new_tick':
                rpc.set([record(0),record(10000)],tick=9999)
            elif action == 'cleared':
                rpc.payload = bytes(rpc.layout.size)
            elif action == 'count_decreased':
                rpc.set([record(0)])
            else:
                rpc.payload = b'badmagic'+rpc.payload[8:]
            with self.assertRaises(live.SessionChanged):
                reader.snapshot()

    def test_reset_during_copy_and_invalid_timestamps_are_rejected(self):
        rpc = MemoryRpc();rpc.set([record(0)])
        def reset(address, size):
            if address == rpc.layout.address+HEADER.size:
                rpc.hook = None;rpc.set([record(0)],tick=9000)
        rpc.hook=reset
        with self.assertRaises(live.SessionChanged):
            live.LiveReader(rpc,rpc.layout).snapshot()
        for rows in ([record(10)], [record(0),record(10000),record(9999)]):
            rpc=MemoryRpc();rpc.set(rows)
            with self.assertRaises(live.CaptureError):
                live.LiveReader(rpc,rpc.layout).snapshot()

    def test_atomic_write_preserves_previous_file_on_replace_failure(self):
        path=self.root/'data.csv';path.write_bytes(b'previous')
        with mock.patch.object(live.os,'replace',side_effect=PermissionError('locked')):
            with self.assertRaises(PermissionError):
                live.atomic_write(path,b'new')
        self.assertEqual(path.read_bytes(),b'previous')
        self.assertEqual(list(self.root.glob('*.tmp')),[])

    def test_partial_backup_is_not_marked_stop_and_latest_csv_lock_keeps_backup(self):
        rpc=MemoryRpc();rpc.set([record(0),record(10000)])
        snap=live.LiveReader(rpc,rpc.layout).snapshot()
        target=self.root/'manual.csv';target.write_bytes(b'old')
        real_replace=live.os.replace
        def locked(source,destination):
            if destination==target:raise PermissionError('Excel locked')
            return real_replace(source,destination)
        with mock.patch.object(live.os,'replace',side_effect=locked):
            backup=live.save_snapshot(snap,target,self.root/'snapshots')
        self.assertEqual(target.read_bytes(),b'old')
        with backup.open() as stream:rows=list(csv.DictReader(stream))
        self.assertEqual(rows[-1]['status'],'RUN')
        self.assertEqual(rows[-1]['elapsed_ms'],'10000')
        self.assertEqual(len(list((self.root/'snapshots').glob('*.csv'))),1)

    def test_locked_final_csv_reports_error_after_preserving_final_backup(self):
        rpc=MemoryRpc();rpc.set([record(0),record(10000,2)],complete=True)
        snap=live.LiveReader(rpc,rpc.layout).snapshot()
        target=self.root/'manual.csv';target.write_bytes(b'old')
        real_replace=live.os.replace
        def locked(source,destination):
            if destination==target:raise PermissionError('Excel locked')
            return real_replace(source,destination)
        with mock.patch.object(live.os,'replace',side_effect=locked):
            with self.assertRaises(live.CaptureError):
                live.save_snapshot(snap,target,self.root/'snapshots')
        self.assertEqual(target.read_bytes(),b'old')
        backup=next((self.root/'snapshots').glob('*_final.csv'))
        self.assertIn(',STOP,',backup.read_text())

    def test_autosaves_every_600_seconds_and_immediately_on_stop(self):
        rpc=MemoryRpc();reader=live.LiveReader(rpc,rpc.layout)
        clock=SimpleNamespace(now=0)
        def sleep(seconds):
            clock.now+=seconds
            if clock.now>=705:
                rpc.set([record(0),record(300000),record(600000),record(700000,2)],complete=True)
            elif clock.now>=605:
                rpc.set([record(0),record(300000),record(600000)])
            elif clock.now>=305:
                rpc.set([record(0),record(300000)])
            elif clock.now>=5:
                rpc.set([record(0)])
        target=self.root/'manual.csv';dump=self.root/'manual.bin'
        class Terminal(io.StringIO):
            def isatty(self):return True
        output=Terminal()
        with contextlib.redirect_stdout(output):
            result=live.collect(reader,target,self.root/'snapshots',dump,autosave_seconds=600,
                                poll_seconds=5,clock=lambda:clock.now,sleep=sleep,
                                progress=live.SampleProgress(stream=output))
        self.assertTrue(result.complete)
        self.assertEqual(clock.now,705)
        self.assertEqual(len(list((self.root/'snapshots').glob('*.csv'))),3)
        with target.open() as stream:rows=list(csv.DictReader(stream))
        self.assertEqual(rows[-1]['status'],'STOP')
        self.assertEqual(rows[-1]['elapsed_ms'],'700000')
        self.assertTrue(decode(dump.read_bytes())[1])
        transcript=output.getvalue()
        # No redraw for repeated polls of the same count. Reset only after a save.
        percentages=re.findall(r'\] +([0-9]+)% \|',transcript)
        self.assertEqual(percentages,['0','50','100','0','16'])
        self.assertIn('+1 mẫu',transcript)
        self.assertIn('+2 mẫu',transcript)
        self.assertEqual(transcript.count('ĐÃ LƯU'),2)
        self.assertIn('HOÀN TẤT (STOP)',transcript)
        self.assertNotIn('Nhận mẫu [',transcript[transcript.rfind('HOÀN TẤT (STOP)'):])

    def test_cli_completed_log_does_not_run_live_gdb_or_touch_firmware(self):
        elf=self.root/'firmware.elf';elf.touch()
        for start_server in (False,True):
            with self.subTest(start_server=start_server):
                rpc=MemoryRpc();rpc.set([record(0),record(10000,2)],complete=True)
                target=self.root/f'manual_{start_server}.csv'
                output=io.StringIO()
                child=SimpleNamespace(poll=lambda:None,terminate=mock.Mock(),wait=mock.Mock())
                def launch(command,**kwargs):
                    # Simulate the actual OpenOCD command-result echo in the screenshot.
                    kwargs['stdout'].write('running\n0x1d776\n0x4f4c5448 0x31303047\n')
                    kwargs['stdout'].flush()
                    return child
                with mock.patch.object(live,'resolve_layout',return_value=rpc.layout), \
                     mock.patch.object(live,'connect_rpc',return_value=contextlib.nullcontext(object())), \
                     mock.patch.object(live,'OpenOcdRpc',return_value=rpc), \
                     mock.patch.object(live.subprocess,'Popen',side_effect=launch) as process, \
                     contextlib.redirect_stdout(output):
                    result=live.main(['--elf',str(elf),'--csv',str(target)]+
                                     (['--start-openocd'] if start_server else []))
                self.assertEqual(result,0)
                self.assertTrue(target.exists())
                self.assertIn('HOÀN TẤT (STOP)',output.getvalue())
                self.assertNotIn('0x4f4c5448',output.getvalue())
                self.assertNotIn('\nrunning\n',output.getvalue())
                if start_server:
                    self.assertIn('0x4f4c5448',(self.root/'openocd.log').read_text())
                    self.assertEqual(process.call_args.kwargs['stderr'],live.subprocess.STDOUT)
                    self.assertTrue(process.call_args.kwargs['stdout'].closed)
                    child.terminate.assert_called_once()
                else:
                    process.assert_not_called()

    def test_cli_will_not_overwrite_a_previous_trial(self):
        elf=self.root/'firmware.elf';elf.touch()
        target=self.root/'manual.csv';target.write_bytes(b'old')
        with mock.patch.object(live,'resolve_layout') as resolve:
            with self.assertRaises(SystemExit):
                live.main(['--elf',str(elf),'--csv',str(target)])
        resolve.assert_not_called()
        self.assertEqual(target.read_bytes(),b'old')


if __name__=='__main__':
    unittest.main()
