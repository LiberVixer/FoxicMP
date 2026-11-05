"""Diagnostic-only passive OS sampling around private continuous SDK tests.

Adds no player requests. It cannot turn a conformance failure into a pass.
Use an empty/new report directory; prior evidence is never overwritten.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import struct
import threading
import time
import traceback

import progressive_continuous_lifecycle_stress as lifecycle
from progressive_continuous_heartbeat_conformance import exercise as heartbeat


class Observer:
    def __init__(self, peers, report, native_failure_dump=False):
        self.peers, self.report = peers, report
        self.began = time.monotonic()
        self.phase = 'lifecycle'
        self.samples, self.calls, self.measurements, self.errors = [], [], [], []
        self.stop = threading.Event()
        self.handles = []
        self.pids = [peer.process.pid for peer in peers]
        self.native_failure_dump = native_failure_dump
        players = []
        for peer in peers:
            executable = Path(peer.player)
            workers = [executable.parent/(name+('.exe' if os.name == 'nt' else ''))
                       for name in ('foxicmp-demux-worker', 'foxicmp-remux-stream-worker')]
            players.append(dict(pid=peer.process.pid, executable=str(executable),
                sha256=lifecycle.sha(executable),
                workerSha256={worker.name:lifecycle.sha(worker) for worker in workers}))
        sources = [Path(__file__), Path(lifecycle.__file__),
                   Path(lifecycle.__file__).with_name('progressive_continuous_heartbeat_conformance.py')]
        (report/'inputs.json').write_text(json.dumps(dict(players=players,
            sourceSha256={source.name:lifecycle.sha(source) for source in sources},
            snapshotAfterOwnedEndpointReady=True, snapshotBeforeScenarios=True), indent=2)+'\n')
        if os.name == 'nt':
            from ctypes import wintypes as w
            self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            self.kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
            self.kernel.OpenProcess.restype = w.HANDLE
            self.kernel.GetProcessHandleCount.argtypes = [w.HANDLE, ctypes.POINTER(w.DWORD)]
            self.kernel.GetProcessHandleCount.restype = w.BOOL
            self.kernel.CloseHandle.argtypes = [w.HANDLE]
            try:
                for pid in self.pids:
                    handle = self.kernel.OpenProcess(0x410, False, pid)
                    if not handle:
                        raise ctypes.WinError(ctypes.get_last_error())
                    self.handles.append(handle)
            except BaseException:
                for handle in self.handles:
                    self.kernel.CloseHandle(handle)
                raise

    def counts(self):
        if os.name != 'nt':
            return [len(list((Path('/proc') / str(pid) / 'fd').iterdir())) for pid in self.pids]
        counts = []
        for handle in self.handles:
            count = ctypes.c_ulong()
            if not self.kernel.GetProcessHandleCount(handle, ctypes.byref(count)):
                raise ctypes.WinError(ctypes.get_last_error())
            counts.append(count.value)
        return counts

    def watch(self):
        try:
            while not self.stop.is_set():
                began = time.monotonic() - self.began
                counts = self.counts()
                self.samples.append(dict(startedSeconds=began, finishedSeconds=time.monotonic()-self.began,
                                         phase=self.phase, handles=counts))
                self.stop.wait(.02)
        except BaseException:
            self.errors.append(traceback.format_exc())

    def capture_failure(self):
        """Only private test processes; never query the installed player's PID."""
        from types import SimpleNamespace
        from progressive_process_metrics import windows_handle_types
        from progressive_windows_handle_trace import Trace
        rows = []
        for pid in self.pids:
            row = dict(pid=pid)
            try:
                row['types'] = windows_handle_types(pid, True)
                row['modules'] = Trace.modules(SimpleNamespace(k=self.kernel, pid=pid))
                if self.native_failure_dump:
                    row['dump'] = self.dump(pid)
            except BaseException:
                row['diagnosticError'] = traceback.format_exc()
            rows.append(row)
        (self.report/'native-failure.json').write_text(json.dumps(rows, indent=2)+'\n')

    def dump(self, pid):
        from ctypes import wintypes as w
        kernel = self.kernel
        kernel.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, ctypes.c_void_p,
                                      w.DWORD, w.DWORD, w.HANDLE]
        kernel.CreateFileW.restype = w.HANDLE
        path = self.report/('private-player-%d.dmp'%pid)
        process = kernel.OpenProcess(0x450, False, pid)
        if not process:
            raise ctypes.WinError(ctypes.get_last_error())
        file = None
        try:
            file = kernel.CreateFileW(str(path), 0x40000000, 0, None, 1, 0x80, None)
            if file == ctypes.c_void_p(-1).value:
                file = None
                raise ctypes.WinError(ctypes.get_last_error())
            dbghelp = ctypes.WinDLL('dbghelp', use_last_error=True)
            dbghelp.MiniDumpWriteDump.argtypes = [w.HANDLE, w.DWORD, w.HANDLE, w.DWORD,
                                                ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
            dbghelp.MiniDumpWriteDump.restype = w.BOOL
            # Handle/thread/module diagnostics, without a full-memory dump.
            if not dbghelp.MiniDumpWriteDump(process, pid, file, 0x1024, None, None, None):
                raise ctypes.WinError(ctypes.get_last_error())
        finally:
            if file is not None:
                kernel.CloseHandle(file)
            kernel.CloseHandle(process)
        data = path.read_bytes()
        assert data[:4] == b'MDMP' and len(data) >= 32, 'Invalid diagnostic dump'
        count, table = struct.unpack_from('<II', data, 8)
        assert count <= 4096 and table+count*12 <= len(data), 'Invalid dump directory'
        kinds = [struct.unpack_from('<I', data, table+i*12)[0] for i in range(count)]
        return dict(file=path.name, bytes=len(data), sha256=lifecycle.sha(path),
                    fullMemory=False, keptLocal=True, requestedFlags='0x1024',
                    actualFlags=hex(struct.unpack_from('<Q', data, 24)[0]),
                    streamTypes=kinds, handleStreamPresent=16 in kinds,
                    handleTypesCapturedSeparately=True)

    def run(self, exercise):
        originals = [peer.client.request for peer in self.peers]
        original_sample = lifecycle.sample
        thread = threading.Thread(target=self.watch, name='private-player-observer')
        for index, peer in enumerate(self.peers):
            original = originals[index]
            def observed(message, timeout=5, *, original=original, index=index):
                event = dict(peer=index, phase=self.phase, action=message['action'],
                             startedSeconds=time.monotonic()-self.began)
                try:
                    reply = original(message, timeout=timeout)
                    event['reply'] = {key:reply[key] for key in ('ok','code','state','errorCode','error',
                        'producerCode','cleanupCode','released','sourceFilesClosed','decoderReaders','workerId') if key in reply}
                    return reply
                except BaseException:
                    event['exception'] = traceback.format_exc()
                    raise
                finally:
                    event['finishedSeconds'] = time.monotonic()-self.began
                    self.calls.append(event)
            peer.client.request = observed
        def measured(pid):
            began = time.monotonic()-self.began
            result = original_sample(pid)
            if pid in self.pids:
                self.measurements.append(dict(peer=self.pids.index(pid), phase=self.phase,
                    startedSeconds=began, finishedSeconds=time.monotonic()-self.began, metrics=result))
            return result
        lifecycle.sample = measured
        thread.start()
        try:
            return exercise()
        except BaseException:
            if os.name == 'nt':
                try:
                    self.capture_failure()
                except BaseException:
                    (self.report/'failure-capture-error.txt').write_text(traceback.format_exc())
            raise
        finally:
            self.stop.set()
            thread.join(2)
            lifecycle.sample = original_sample
            for peer, original in zip(self.peers, originals):
                peer.client.request = original
            for handle in self.handles:
                self.kernel.CloseHandle(handle)
            assert not thread.is_alive(), 'Private observer did not stop'
            for name, data in (('os-samples', self.samples), ('observed-rpc', self.calls),
                               ('budget-measurements', self.measurements), ('observer-errors', self.errors)):
                (self.report/(name+'.json')).write_text(json.dumps(data, indent=2)+'\n')
            assert not self.errors, self.errors


def main(args):
    report = Path(args.report_directory).resolve()
    report.mkdir(parents=True, exist_ok=False)
    original = lifecycle.exercise
    original_heartbeat = lifecycle.heartbeat_exercise
    def exercise(peers, root, output, cycles, repeats):
        observer = Observer(peers, output, getattr(args, 'native_failure_dump', False))
        heartbeat_count = 0
        def timed_heartbeat(*positional, **keywords):
            nonlocal heartbeat_count
            heartbeat_count += 1
            observer.phase = 'heartbeat-%02d'%heartbeat_count
            try:
                return original_heartbeat(*positional, **keywords)
            finally:
                observer.phase = 'after-heartbeat-%02d'%heartbeat_count
        lifecycle.heartbeat_exercise = timed_heartbeat
        def observed():
            if args.mode == 'lifecycle':
                return original(peers, root, output, cycles, repeats)
            rows = []
            for index in range(args.rounds):
                observer.phase = 'full-heartbeat-%02d'%(index+1)
                directory = root/observer.phase
                directory.mkdir()
                destination = output/observer.phase
                destination.mkdir()
                result = heartbeat(peers, directory, destination)
                rows.append(result)
                (output/'rounds.json').write_text(json.dumps(rows, indent=2)+'\n')
            return dict(result='PASS', rounds=rows, count=sum(row['count'] for row in rows))
        return observer.run(observed)
    lifecycle.exercise = exercise
    try:
        result = lifecycle.staged(args, report)
        result.update(diagnosticOnly=True, fullStressAcceptance=False, mode=args.mode,
            observerSourceSha256=lifecycle.sha(Path(__file__)), noAdditionalPlayerRequests=True,
            passiveSamplingIntervalMilliseconds=20)
        (report/'result.json').write_text(json.dumps(result, indent=2)+'\n')
    except BaseException:
        (report/'error.txt').write_text(traceback.format_exc())
        raise
    finally:
        lifecycle.exercise = original
        lifecycle.heartbeat_exercise = original_heartbeat


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--edition', choices=('qt','classic'), required=True)
    parser.add_argument('--player', action='append', required=True)
    parser.add_argument('--reader-runner', required=True)
    parser.add_argument('--worker', default='')
    parser.add_argument('--runtime-directory', default='')
    parser.add_argument('--report-directory', required=True)
    parser.add_argument('--mode', choices=('lifecycle','full-heartbeat'), required=True)
    parser.add_argument('--cycles', type=int, default=100)
    parser.add_argument('--heartbeat-repeats', type=int, default=12)
    parser.add_argument('--rounds', type=int, default=2)
    parser.add_argument('--native-failure-dump', action='store_true')
    args = parser.parse_args()
    assert 100 <= args.cycles <= 200 and 6 <= args.heartbeat_repeats <= 20 and 1 <= args.rounds <= 3
    main(args)
