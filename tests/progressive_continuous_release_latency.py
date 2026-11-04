"""Repeat the real paused writer-heartbeat path without shortening its deadline.

Each iteration preserves status-only close timing before any diagnostic requests.
The two private peers survive between iterations; no user player is stopped.
"""
import argparse
import json
from pathlib import Path
import time
import traceback

from progressive_continuous_api_test import run
from progressive_continuous_heartbeat_conformance import exercise
from progressive_player_crash_recovery import sha


def repeat(peers, root, report, count):
    rows = []
    for index in range(count):
        name = 'attempt-%02d' % (index+1)
        directory, output = root/name, report/name
        directory.mkdir()
        output.mkdir()
        started = time.monotonic()
        result = exercise(peers, directory, output, profiles=('paused',), completed=False)
        assert result['count']==1 and result['result']=='PASS', result
        row = result['rows'][0]
        assert row['profile']=='paused' and row['automaticReleaseSeconds']<2, row
        rows.append(dict(attempt=index+1, seconds=time.monotonic()-started,
            maxRequestSeconds=result['maxRequestSeconds'], **row))
        (report/'progress.json').write_text(json.dumps(dict(completed=len(rows),rows=rows),indent=2)+'\n',encoding='utf-8')
        print(json.dumps(dict(attempt=index+1,result='PASS',automaticReleaseSeconds=row['automaticReleaseSeconds'])),flush=True)
    return dict(result='PASS',count=len(rows),rows=rows,deadlineMilliseconds=30000,
        releaseBudgetSeconds=2,heartbeatConformanceSourceSha256=sha(Path(__file__).with_name('progressive_continuous_heartbeat_conformance.py')),
        sharedPrivatePeers=True,clockNotMocked=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--edition',choices=('qt',),required=True)
    parser.add_argument('--player',action='append',required=True)
    parser.add_argument('--reader-runner',required=True)
    parser.add_argument('--report-directory',required=True)
    parser.add_argument('--repeat',type=int,default=6)
    args=parser.parse_args()
    assert 1<=args.repeat<=20
    report=Path(args.report_directory).resolve()
    report.mkdir(parents=True,exist_ok=True)
    try:
        run(args,lambda peers,root:repeat(peers,root,report,args.repeat),__file__)
    except Exception:
        (report/'error.txt').write_text(traceback.format_exc(),encoding='utf-8')
        raise
