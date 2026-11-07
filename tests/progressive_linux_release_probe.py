"""Linux-only passive /proc diagnostics around the real writer deadline.

No player hooks, clock changes or additional release-path RPCs. An optional
private registry-lock hold is an explicitly separate fault reproduction, never
an ordinary latency acceptance result.
"""
import argparse
import hashlib
import importlib
import json
import os
from pathlib import Path
import re
import sys
import threading
import time
import traceback
from types import SimpleNamespace

sys.dont_write_bytecode = True


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def process_birth(pid):
    # comm may itself contain parentheses/spaces; starttime is field 22.
    return (Path('/proc')/str(pid)/'stat').read_text().rsplit(')', 1)[1].split()[19]


class Probe:
    def __init__(self, peers, report, hold_seconds):
        self.peers = peers
        self.report = report
        self.hold_seconds = hold_seconds
        self.began = time.monotonic()
        self.births = {p.process.pid: process_birth(p.process.pid) for p in peers}
        self.windows = []
        self.calls = []
        self.errors = []
        self.holds = []
        self.stop = threading.Event()
        self.lock = threading.Lock()
        self.hold_threads = []
        self.syscalls = {}
        header = Path('/usr/include/x86_64-linux-gnu/asm/unistd_64.h')
        if os.uname().machine == 'x86_64' and header.exists():
            self.syscalls = {int(n): name for name, n in re.findall(r'#define __NR_(\w+) (\d+)', header.read_text())}

    def snapshot(self, pid):
        assert process_birth(pid) == self.births[pid], 'Owned process identity changed'
        root = Path('/proc')/str(pid)
        threads = []
        for task in sorted((root/'task').iterdir()):
            row = dict(tid=int(task.name))
            try:
                row['comm'] = (task/'comm').read_text().strip()
                row['wchan'] = (task/'wchan').read_text().strip()
                row['schedstat'] = (task/'schedstat').read_text().strip()
                raw = (task/'syscall').read_text().strip()
                row['syscall'] = raw
                if raw and raw.split()[0].isdigit():
                    row['syscallName'] = self.syscalls.get(int(raw.split()[0]), 'unknown')
            except FileNotFoundError:
                row['exitedDuringSnapshot'] = True
            except PermissionError:
                row['syscallPermissionDenied'] = True
            threads.append(row)
        files = []
        for file in sorted((root/'fd').iterdir()):
            try:
                target = os.readlink(file)
            except FileNotFoundError:
                continue
            if target.endswith(('.lease', 'output.fmp4', '/.guard', 'input.part')):
                files.append(dict(fd=int(file.name), target=target))
        return dict(pid=pid, birth=self.births[pid], threads=threads, mediaAndReceiptFiles=files)

    def watch(self):
        try:
            with (self.report/'proc.jsonl').open('w', encoding='utf-8') as file:
                first = True
                while not self.stop.is_set():
                    now = time.monotonic()
                    with self.lock:
                        windows = list(self.windows)
                    active = [w for w in windows if 29 <= now-w['acceptedAt'] <= 35]
                    if first or active:
                        row = dict(seconds=now-self.began, nearDeadlineJobs=[w['jobId'] for w in active],
                                   peers=[self.snapshot(p.process.pid) for p in self.peers])
                        file.write(json.dumps(row)+'\n')
                        file.flush()
                        first = False
                    self.stop.wait(.05)
        except BaseException:
            self.errors.append(traceback.format_exc())

    def hold_registry(self, window):
        import fcntl
        path = self.peers[0].registry/'.guard'
        began = window['acceptedAt']+29.5
        if self.stop.wait(max(0, began-time.monotonic())):
            return
        record = dict(jobId=window['jobId'], path=str(path), requestedSeconds=self.hold_seconds)
        fd = None
        try:
            fd = os.open(path, os.O_RDWR | os.O_NOFOLLOW | os.O_CLOEXEC)
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            record['lockedAt'] = time.monotonic()-self.began
            stat = os.fstat(fd)
            record.update(device=stat.st_dev, fileId=stat.st_ino)
            self.stop.wait(self.hold_seconds)
        except BaseException:
            record['error'] = traceback.format_exc()
        finally:
            if fd is not None:
                fcntl.flock(fd, fcntl.LOCK_UN)
                os.close(fd)
            record['unlockedAt'] = time.monotonic()-self.began
            self.holds.append(record)
            (self.report/'registry-holds.json').write_text(json.dumps(self.holds, indent=2)+'\n')

    def run(self, exercise):
        originals = [p.client.request for p in self.peers]
        with (self.report/'rpc.jsonl').open('w', encoding='utf-8') as file:
            for index, peer in enumerate(self.peers):
                original = originals[index]
                def observed(message, timeout=5, *, index=index, original=original):
                    began = time.monotonic()
                    row = dict(peer=index, action=message['action'], began=began-self.began)
                    try:
                        reply = original(message, timeout=timeout)
                        row['reply'] = {k: reply[k] for k in ('state','errorCode','cleanupCode','producerCode','sourceFilesClosed','released','decoderReaders','workerId') if k in reply}
                        if index==0 and message['action']=='continuousRemuxHeartbeat':
                            window = dict(jobId=message['jobId'], requestAt=began, acceptedAt=time.monotonic())
                            with self.lock:
                                self.windows.append(window)
                            if self.hold_seconds:
                                thread = threading.Thread(target=self.hold_registry, args=(window,))
                                self.hold_threads.append(thread)
                                thread.start()
                        return reply
                    except BaseException:
                        row['error'] = traceback.format_exc()
                        raise
                    finally:
                        row['finished'] = time.monotonic()-self.began
                        row['seconds'] = row['finished']-row['began']
                        self.calls.append(row)
                        file.write(json.dumps(row)+'\n')
                        file.flush()
                peer.client.request = observed
            thread = threading.Thread(target=self.watch)
            thread.start()
            try:
                return exercise()
            finally:
                primary_failure = sys.exc_info()[0] is not None
                self.stop.set()
                thread.join(3)
                for hold in self.hold_threads:
                    hold.join(3)
                    if hold.is_alive():
                        self.errors.append('Registry hold thread did not stop')
                for peer, original in zip(self.peers, originals):
                    peer.client.request = original
                if thread.is_alive():
                    self.errors.append('Proc observer did not stop')
                (self.report/'probe.json').write_text(json.dumps(dict(pids=list(self.births), births=self.births,
                    windows=self.windows, samplingMilliseconds=50, noAdditionalPlayerRequests=True,
                    syscallMap=self.syscalls, errors=self.errors, registryHolds=self.holds,
                    maxRpcSeconds=max((r['seconds'] for r in self.calls),default=0)),indent=2)+'\n')
                if not primary_failure:
                    assert not self.errors and all('error' not in hold for hold in self.holds), (self.errors,self.holds)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--player', type=Path, required=True)
    parser.add_argument('--reader-runner', type=Path, required=True)
    parser.add_argument('--sdk-directory', type=Path, required=True)
    parser.add_argument('--report-directory', type=Path, required=True)
    parser.add_argument('--repeat', type=int, default=2)
    parser.add_argument('--registry-hold-seconds', type=float, default=0)
    args = parser.parse_args()
    assert sys.platform=='linux' and 1<=args.repeat<=4 and 0<=args.registry_hold_seconds<=4
    report = args.report_directory.resolve()
    report.mkdir(parents=True, exist_ok=False)
    sys.path.insert(0, str(args.sdk_directory.resolve()))
    names = ('progressive_schema','progressive_client','progressive_integrity','progressive_resources',
             'progressive_sdk','progressive_v2','progressive_continuous','progressive_windows','progressive_preview','progressive_writer')
    modules = {name: importlib.import_module(name) for name in names}
    assert all(Path(m.__file__).resolve().parent==args.sdk_directory.resolve() for m in modules.values())
    from progressive_continuous_api_test import run
    from progressive_continuous_heartbeat_conformance import exercise as heartbeat
    (report/'inputs.json').write_text(json.dumps(dict(playerSha256=digest(args.player),
        sourceSha256=digest(__file__), helperSha256=digest(Path(__file__).with_name('progressive_continuous_heartbeat_conformance.py')),
        sdkSha256={name:digest(m.__file__) for name,m in modules.items()},
        registryHoldSeconds=args.registry_hold_seconds, ordinaryAcceptance=not args.registry_hold_seconds),indent=2)+'\n')
    def exercise(peers, root):
        probe = Probe(peers, report, args.registry_hold_seconds)
        def cases():
            results = []
            for index in range(args.repeat):
                work = root/str(index);work.mkdir()
                output = report/('attempt-%02d'%index);output.mkdir()
                results.append(heartbeat(peers,work,output,profiles=('paused',),completed=False))
            return dict(result='PASS',count=len(results),rows=results,diagnosticOnly=True,
                        registryHoldSeconds=args.registry_hold_seconds,historicalOutlierResolved=False)
        return probe.run(cases)
    try:
        result = run(SimpleNamespace(edition='qt',player=[str(args.player.resolve())],reader_runner=str(args.reader_runner),report_directory=str(report)),exercise_case=exercise,source_file=__file__)
    except BaseException:
        (report/'error.txt').write_text(traceback.format_exc())
        raise
    print(json.dumps(dict(result=result['result'],count=result['count'])))


if __name__=='__main__':
    main()
