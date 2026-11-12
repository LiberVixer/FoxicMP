"""Stricter Linux memory gate over the complete, uninstrumented A/V soak.

Run after progressive_aac_package_test.py --scenario aac-soak, without smoke.
Compare equal playback windows across jobs so a growing current index is not
confused with memory retained from previous jobs. No allocator tuning/trim.
"""
import argparse
import json
from pathlib import Path


TOTAL_GROWTH = 32 * 1024**2
WARM_REPEAT_GROWTH = 8 * 1024**2


def verify(result):
    assert result['result'] == 'PASS' and result['soakAcceptance']
    assert not result['smoke'] and result['elapsedSeconds'] >= 1800
    assert result['edition'] == 'qt' and len(result['sameMainPids']) == 2
    assert len(result['runs']) == 3
    samples = result['samples']
    assert len(samples) >= 350
    assert all(m['memorySource'] == 'proc-status-VmRSS-RssAnon'
               for row in samples for m in row['mainMetrics'])
    early = [row for row in samples if row['cycle'] == 0 and 60 <= row['cycleSeconds'] <= 90]
    late = [row for row in samples if row['seconds'] >= samples[-1]['seconds'] - 60]
    repeats = [[row for row in samples if row['cycle'] == cycle and 60 <= row['cycleSeconds'] <= 90]
               for cycle in (1, 2)]
    assert len(early) >= 5 and len(late) >= 10 and all(len(rows) >= 5 for rows in repeats)
    peers = []
    for peer in range(2):
        values = {}
        for key in ('rssBytes', 'privateBytes'):
            high = lambda rows: max(row['mainMetrics'][peer][key] for row in rows)
            growth = high(late) - high(early)
            repeat_growth = high(repeats[1]) - high(repeats[0])
            values[key] = dict(totalGrowthBytes=growth, warmedRepeatGrowthBytes=repeat_growth)
        peers.append(dict(peer=peer, metrics=values))
    failures = [dict(peer=row['peer'], metric=key, **value)
                for row in peers for key, value in row['metrics'].items()
                if value['totalGrowthBytes'] > TOTAL_GROWTH
                or value['warmedRepeatGrowthBytes'] > WARM_REPEAT_GROWTH]
    return dict(result='FAIL' if failures else 'PASS', peers=peers, failures=failures,
                totalGrowthBudgetBytes=TOTAL_GROWTH, warmedRepeatGrowthBudgetBytes=WARM_REPEAT_GROWTH,
                elapsedSeconds=result['elapsedSeconds'], samples=len(samples),
                physicalAvSyncMeasured=False)


def verify_lifecycle(result):
    assert result['result'] == 'PASS' and result['count'] == result['cycles'] == 100
    # AAC intentionally has no legacy video-only heartbeat repeats. This is a
    # memory gate for all 100 real cycles, not the wider full-stress claim.
    assert result['edition'] == 'qt' and result['samePrivatePlayers']
    assert result['publicMode'] == 'continuous-h264-aac-window-v1'
    rows = result['rows']
    assert len(rows) == 100
    early, steady, late = rows[:10], rows[30:40], rows[-10:]
    assert all(m['memorySource'] == 'proc-status-VmRSS-RssAnon'
               for row in rows for m in row['metrics'])
    peers, failures = [], []
    for peer in range(2):
        values = {}
        for key in ('rssBytes', 'privateBytes'):
            high = lambda window: max(row['metrics'][peer][key] for row in window)
            growth, repeat_growth = high(late) - high(early), high(late) - high(steady)
            values[key] = dict(totalGrowthBytes=growth, warmedRepeatGrowthBytes=repeat_growth)
            if growth > TOTAL_GROWTH or repeat_growth > WARM_REPEAT_GROWTH:
                failures.append(dict(peer=peer, metric=key, **values[key]))
        peers.append(dict(peer=peer, metrics=values))
    return dict(result='FAIL' if failures else 'PASS', peers=peers, failures=failures,
                totalGrowthBudgetBytes=TOTAL_GROWTH, warmedRepeatGrowthBudgetBytes=WARM_REPEAT_GROWTH,
                cycles=100, elapsedSeconds=result['wallSeconds'], physicalAvSyncMeasured=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lifecycle', action='store_true', help='Verify aac-lifecycle (100 file changes) instead')
    parser.add_argument('soak_result', type=Path)
    parser.add_argument('report', type=Path)
    args = parser.parse_args()
    value = (verify_lifecycle if args.lifecycle else verify)(json.loads(args.soak_result.read_text()))
    args.report.write_text(json.dumps(value, indent=2) + '\n')
    print(json.dumps(value))
    raise SystemExit(0 if value['result'] == 'PASS' else 1)
