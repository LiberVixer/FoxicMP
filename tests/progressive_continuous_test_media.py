"""Media selection shared by public continuous playback resilience suites."""
import copy
import os

from progressive_backend_demux_conformance import snapshot
from progressive_stream_remux_worker_test import fixture, probe
from progressive_continuous import MODE, AAC_MODE


def source_fixture(root, name, mode=MODE, seconds=24):
    if mode == MODE:
        return fixture(root, name, seconds=seconds)
    assert mode == AAC_MODE, mode
    from progressive_stream_aac_aligned_test import fixture as av_fixture
    path, _ = av_fixture(root, name, 48000, 2, '30000/1001', 3, False,
                         False, seconds=seconds, samples=48000*seconds-137)
    return path


def source_snapshot(path, raw, *, complete=False, prefix=None, mode=MODE):
    model = snapshot(path, raw, complete=complete, prefix=prefix)
    model['timeline']['duration'] = '243000000' if mode == AAC_MODE else '240000000'
    if mode == AAC_MODE:
        model['tracks'][0]['codecHint'] = 'h264'
        audio = copy.deepcopy(model['tracks'][0])
        audio.update(id='sound', kind='audio', codecHint='aac-lc')
        model['tracks'].append(audio)
    return model


def source_cuts(path, mode=MODE):
    packets = probe(path)['packets']
    return {second: int(next(p['pos'] for p in packets
                            if (mode != AAC_MODE or p['codec_type'] == 'video')
                            and float(p['pts_time']) >= second))+13
            for second in (12, 20)}


def audible_decoder_ready(status, edition, mode=MODE):
    if mode != AAC_MODE:
        return True
    if not status['pcmWindowApplied']:
        return False
    assert status['audio'], status
    if edition == 'qt':
        assert status['audioCodec'] == 'aac', status
        assert status['audioOutput'] == ('wasapi' if os.name == 'nt' else 'pulse'), status
    return True
