"""SimpleBlock VFR preparation only: incremental prototype + existing consumers."""
import argparse
from array import array
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import tempfile
import traceback
import zlib

from progressive_backend_demux_conformance import snapshot
from progressive_integrity import protect_snapshot
from progressive_simple_vfr_prototype import SimpleVfrPrototype, Invalid
from progressive_stream_remux_worker_test import serve
from progressive_webm_fixtures import element, width
from progressive_webm_opus_remux_preflight import children, configuration, packets, pcm, probe
from progressive_webm_remux_preflight import command, ok, scan, sha, unknown_clusters
from progressive_webm_vfr_preflight import PROFILES, fixture, decoded, ebml, unsigned


def head_without_default(raw):
    layout = scan(raw);_, _, segment_at = element(raw, 0)
    _, start, _ = element(raw, segment_at)
    def rewrite(a, z, video=False):
        out = bytearray()
        for tag, at, b, end in children(raw, a, z):
            if tag in (0x114d9b74, 0xbf) or (video and tag == 0x23e383):continue
            if tag == 0x1654ae6b:out += ebml(tag, rewrite(b, end))
            elif tag == 0xae:
                kind = next(int.from_bytes(raw[x:y], 'big') for t, _, x, y in children(raw, b, end) if t == 0x83)
                out += ebml(tag, rewrite(b, end, kind == 1))
            else:out += raw[at:end]
        return bytes(out)
    return raw[:start]+rewrite(start, layout['headBytes'])+raw[layout['headBytes']:]


def decorate(raw, unknown):
    """Valid input metadata whose byte offsets/checksums must not survive rewriting."""
    layout = scan(raw);_, _, s = element(raw, 0);_, segment, _ = element(raw, s)
    head = raw[:layout['headBytes']];seek = b''
    for _ in range(8):
        pos = len(head)+len(seek)-segment
        value = ebml(0x114d9b74, ebml(0x4dbb, ebml(0x53ab, bytes.fromhex('1f43b675'))+unsigned(0x53ac, pos)))
        if value == seek:break
        seek = value
    else:raise AssertionError('SeekHead length did not converge')
    out = bytearray(head+seek);first = len(out)-segment;previous = None;checksums = []
    for c in layout['clusters']:
        body = unsigned(0xa7, len(out)-segment)
        if previous is not None:body += unsigned(0xab, previous)
        body += ebml(0xec, b'payload-not-a-boundary:\x1f\x43\xb6\x75\xff')
        body += raw[c['body']:c['end']]
        crc = zlib.crc32(body).to_bytes(4, 'little');body = ebml(0xbf, crc)+body
        value = ebml(0x1f43b675, body);out += value;previous = len(value);checksums.append(crc.hex())
    out += ebml(0x1c53bb6b, ebml(0xbb, unsigned(0xb3, 0)+ebml(0xb7, unsigned(0xf7, configuration(raw)[1]['number'])+unsigned(0xf1, first))))
    value = bytes(out)
    if unknown:value = unknown_clusters(value, scan(value))
    # Verify the synthetic source checksums independently before the prototype.
    for c in scan(value)['clusters']:
        fields = list(children(value, c['body'], c['end']))
        tag, at, b, e = fields[0]
        assert tag == 0xbf and int.from_bytes(value[b:e], 'little') == zlib.crc32(value[e:c['end']])
    return value, len(checksums)


def clusters_in(raw):
    _, _, segment_at = element(raw, 0);_, at, _ = element(raw, segment_at);count = 0
    while at < len(raw):
        tag, _, end = element(raw, at);assert end is not None and end <= len(raw)
        count += tag == 0x1f43b675;at = end
    return count


def conversion(raw, frames, end, chunk):
    p = SimpleVfrPrototype(len(raw), frames, end);out = bytearray();first = None
    for at in range(0, len(raw), chunk):
        out += p.feed(raw[at:at+chunk])
        assert p.at <= p.supplied <= len(raw)
        if first is None and p.head_ready and clusters_in(out):first = p.supplied
    assert not p.finished and p.pending is not None
    before = clusters_in(out);assert before < p.clusters
    assert p.feed() == b'' and p.waiting == 'source-complete'
    out += p.feed(complete=True)
    assert p.finished and p.pending is None and p.at == len(raw)
    assert first is not None and first < len(raw)
    assert clusters_in(out) == p.clusters and p.peak_pending <= p.limit
    return bytes(out), dict(chunk=chunk, firstClusterBeforeCompleteAt=first,
                           inputBytes=len(raw), peakPendingBytes=p.peak_pending,
                           peakInputBufferBytes=p.peak_input, droppedMetadata=p.dropped,
                           clusters=p.clusters, clustersBeforeComplete=before,
                           lastFrameHeldUntilConfirmedEof=True, readConfirmedOnly=True)


def lookahead(raw, frames, end):
    layout = scan(raw);video = configuration(raw)[1]['number'];second = layout['clusters'][1]
    next_frame = next(b for b in layout['blocks'] if b['track'] == video and second['body'] <= b['offset'] < second['end'])
    p = SimpleVfrPrototype(len(raw), frames, end)
    out = bytearray()
    for at in range(0, layout['clusters'][0]['end'], 4093):
        out += p.feed(raw[at:min(at+4093, layout['clusters'][0]['end'])])
    assert clusters_in(out) == 0 and p.pending is not None
    while p.supplied < next_frame['end']:
        at = p.supplied;out += p.feed(raw[at:min(at+4093,next_frame['end'])])
    assert clusters_in(out) == 1 and p.supplied < second['end']
    return dict(firstClusterWaitedForNextVideo=True,
                firstClusterReleasedBeforeNextClusterEnd=True, nextVideoBodyEnd=next_frame['end'])


def negative_controls(raw, frames, end):
    layout = scan(raw);v = configuration(raw)[1]['number'];video = [b for b in layout['blocks'] if b['track'] == v]
    rows = []
    def attempt(label, value, count=frames, endpoint=end, complete=True, limit=32*1024*1024):
        try:
            p = SimpleVfrPrototype(len(value),count,endpoint,pending_limit=limit)
            for at in range(0,len(value),4093):p.feed(value[at:at+4093])
            p.feed(complete=complete)
        except (Invalid,ValueError) as e:rows.append(dict(case=label,code=str(e)));return
        raise AssertionError(('Malformed input admitted',label))
    for label,delta in [('duplicate',-40),('regression',-80),('long-gap',2000)]:
        data = bytearray(raw);b = video[1];w = width(data[b['body']]);at = b['body']+w
        ticks = int.from_bytes(data[at:at+2],'big',signed=True)+delta;data[at:at+2] = ticks.to_bytes(2,'big',signed=True)
        attempt(label,bytes(data))
    attempt('short-count',raw,frames-1);attempt('long-count',raw,frames+1)
    attempt('short-final-end',raw,endpoint=int(video[-1]['ticks'])*10000)
    attempt('long-final-end',raw,endpoint=end+20000000)
    attempt('pending-budget',raw,limit=64)
    for cut in (1,layout['headBytes']-1,len(raw)-1):
        p = SimpleVfrPrototype(len(raw),frames,end)
        try:
            for at in range(0,cut,4093):p.feed(raw[at:min(at+4093,cut)])
            p.feed(complete=True)
        except Invalid as e:rows.append(dict(case='premature-eof-'+str(cut),code=str(e)))
        else:raise AssertionError('Premature source complete accepted')
    return rows


def consumer(runner, worker, edition, raw, original, frames, end, channels, folder):
    """Existing VFR decoder probe, no new SimpleBlock player capability."""
    with tempfile.TemporaryDirectory(prefix='fox-simple-vfr-pcm-') as t:
        root = Path(t);path = root/'video.part'
        model = snapshot(path,raw,complete=True);model['timeline']['duration'] = str(end)
        model['tracks'][0].update(codecHint='vp9',containerHint='webm')
        import copy
        sound = copy.deepcopy(model['tracks'][0]);sound.update(id='sound',kind='audio',codecHint='opus');model['tracks'].append(sound)
        model = protect_snapshot(model,root);manifest = root/'session.json';manifest.write_text(json.dumps(model));pcmfile = root/'actual.pcm'
        p = command([runner,manifest,worker,root/'registry',root/'cache',pcmfile,str(channels),'1152000','vfr',str(frames),str(end)],folder,'decoder')
        assert p.returncode == 0,(p.returncode,p.stderr[-1200:])
        fmt = 's16le' if edition == 'qt' else 'f32le';codec = 'pcm_s16le' if edition == 'qt' else 'pcm_f32le'
        expected = ok(['ffmpeg','-v','error','-threads','1','-i',original,'-map','0:a:0','-c:a',codec,'-f',fmt,'-'],folder,'decoder-reference')
        (folder/'decoder-reference.stdout').unlink();actual = pcmfile.read_bytes()
        stride = channels*(2 if edition == 'qt' else 4);assert len(actual) == len(expected) == 1152000*stride
        a,b = array('h' if edition == 'qt' else 'f'),array('h' if edition == 'qt' else 'f');a.frombytes(actual);b.frombytes(expected)
        difference = max(abs(x-y) for x,y in zip(a,b));assert difference <= (2 if edition == 'qt' else 2e-6)
        assert not list((root/'cache').glob('remux-*')) and path.read_bytes() == raw
        return dict(exitCode=0,samples=1152000,maxAbsolutePcmError=difference,exactSampleCount=True,
                    originalSourceUnchanged=True,audioDeviceOpened=False,existingVfrAdapter=True)


def run(worker,runner,edition,report):
    report = Path(report).resolve();report.mkdir(parents=True,exist_ok=True);rows = []
    result = dict(result='RUNNING',scope='test-only-prototype-no-new-public-mode',
                  sdkVersionUnchanged='1.101',applicationVersionUnchanged='0.2.0',
                  workerSha256=sha(worker),decoderSha256=sha(runner),testSourceSha256=sha(__file__),
                  prototypeSha256=sha(Path(__file__).with_name('progressive_simple_vfr_prototype.py')),
                  ordinaryMainSimpleBlockPlaybackTested=False,physicalAvSyncMeasured=False,rows=rows)
    try:
        for profile in PROFILES:
            folder = report/profile[0];original,pixels = fixture(folder,profile)
            before = sha(original);metadata = probe(original,folder,'original-probe');video = packets(metadata,'video')
            frames = len(video);base = scan(original.read_bytes())['defaultDurationNs']
            end = int(Fraction(video[-1][1])*10000000)+round(Fraction(base,1000000))*10000
            simple = head_without_default(original.read_bytes());simple_path = folder/'simple.webm';simple_path.write_bytes(simple)
            reference_pcm = pcm(original,folder,'original-pcm');images = decoded(original,pixels,folder,'original-video')
            for unknown in (False,True):
                case = folder/('unknown' if unknown else 'finite');case.mkdir(exist_ok=True)
                raw,crcs = decorate(simple,unknown);source = case/'input.webm';source.write_bytes(raw);digest = sha(source)
                ref = probe(source,case,'simple-probe')
                for kind in ('video','audio'):assert packets(ref,kind) == packets(metadata,kind)
                outputs = [conversion(raw,frames,end,chunk) for chunk in (257,8191,65536)]
                output = outputs[0][0];assert all(value == output for value,_ in outputs)
                expected_layout = scan(output)
                for c in expected_layout['clusters']:
                    assert not c['unknownSize']
                    assert all(t not in (0xbf,0xa7,0xab) for t,_,_,_ in children(output,c['body'],c['end']))
                _,_,seg = element(output,0);_,top,_ = element(output,seg)
                assert all(t not in (0x114d9b74,0x1c53bb6b,0xbf) for t,_,_,_ in children(output,top,len(output)))
                sealed = case/'output.webm';sealed.write_bytes(output);other = probe(sealed,case,'output-probe')
                for kind in ('video','audio'):assert packets(other,kind) == packets(ref,kind)
                assert pcm(sealed,case,'output-pcm') == reference_pcm and decoded(sealed,pixels,case,'output-video') == images
                refused,_ = serve(worker,source,mode='continuous-webm-vp9-opus-vfr-v1',format_hint='webm',video_samples=frames,video_duration=end,audio_samples=1152000,growing=False)
                assert refused['exitCode'] == 1 and refused['result']['code'] == 'unsupported-stream-webm-video-duration'
                accepted,cache = serve(worker,sealed,mode='continuous-webm-vp9-opus-vfr-v1',format_hint='webm',video_samples=frames,video_duration=end,audio_samples=1152000)
                assert accepted['exitCode'] == 0 and accepted['result']['code'] == 'data' and accepted['firstCompleteFragmentBeforeSourceComplete']
                check_pcm = consumer(runner,worker,edition,output,source,frames,end,profile[3],case)
                negative = negative_controls(raw,frames,end) if not rows else []
                assert sha(original) == before and sha(source) == digest
                rows.append(dict(profile=profile[0],unknownClusters=unknown,frames=frames,end100ns=end,
                                 validInputClusterCrcCount=crcs,readPatterns=[x for _,x in outputs],
                                 lookahead=lookahead(raw,frames,end),negativeControls=negative,
                                 sourceSha256=digest,outputSha256=sha(sealed),exactPacketsAndClocks=True,
                                 exactPcm=True,decodedFramesIdentical=True,sourceUnchanged=True,
                                 directSimpleBlockRefusal=dict(exitCode=refused['exitCode'],code=refused['result']['code']),
                                 existingVfrWorker=dict(exitCode=accepted['exitCode'],code=accepted['result']['code']),decoder=check_pcm))
                (report/'progress.json').write_text(json.dumps(dict(count=len(rows))))
        result.update(result='PASS-preflight-only',count=len(rows));(report/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        print('SimpleBlock VFR preflight PASS',len(rows));return result
    except BaseException:
        result.update(result='FAIL',error=traceback.format_exc());(report/'failure.json').write_text(json.dumps(result,indent=2)+'\n');raise


if __name__ == '__main__':
    p = argparse.ArgumentParser();p.add_argument('--worker',type=Path,required=True);p.add_argument('--runner',type=Path,required=True)
    p.add_argument('--edition',choices=('qt','classic'),required=True);p.add_argument('--report-directory',required=True)
    a = p.parse_args();run(a.worker.resolve(),a.runner.resolve(),a.edition,a.report_directory)
