"""Growing VFR worker, exact clocks/PCM and malformed-duration controls."""
import argparse
import hashlib
import json
from pathlib import Path
import tempfile

from progressive_stream_remux_worker_test import serve
from progressive_webm_fixtures import element, width
from progressive_webm_opus_remux_preflight import children, configuration, packets, pcm, probe
from progressive_webm_remux_preflight import scan, unknown_clusters
from progressive_webm_vfr_fixtures import CASES, media
from progressive_webm_vfr_preflight import decoded, ebml, unsigned

MODE = 'continuous-webm-vp9-opus-vfr-v1'


def corrupt(raw, target, mutation):
    layout = scan(raw)
    track = configuration(raw)[1]['number']
    out = bytearray(raw[:layout['headBytes']])
    count = 0
    for c in layout['clusters']:
        body = bytearray()
        for tag, at, b, e in children(raw, c['body'], c['end']):
            if tag == 0xa0:
                fields = list(children(raw, b, e))
                block = next(raw[x:y] for k, _, x, y in fields if k == 0xa1)
                if block[0] == 0x80 | track:
                    if count == target:
                        if mutation == 'lost-frame':
                            count += 1
                            continue
                        group = bytearray()
                        for k, start, x, y in fields:
                            if k == 0x9b and mutation in ('missing-duration', 'zero-duration', 'huge-duration', 'last-short'):
                                if mutation != 'missing-duration':
                                    n = 0 if mutation == 'zero-duration' else 1001 if mutation == 'huge-duration' else int.from_bytes(raw[x:y], 'big')-1
                                    group += unsigned(k, n)
                            elif k == 0xa1 and mutation in ('gap', 'duplicate', 'regression'):
                                data = bytearray(raw[x:y]);w = width(data[0])
                                tick = int.from_bytes(data[w:w+2], 'big', signed=True)
                                tick += 2 if mutation == 'gap' else -40 if mutation == 'duplicate' else -80
                                data[w:w+2] = tick.to_bytes(2, 'big', signed=True)
                                group += ebml(k, data)
                            elif k == 0xfb and mutation == 'missing-reference':
                                continue
                            else:
                                group += raw[start:y]
                        if mutation == 'duplicate-duration':
                            group += unsigned(0x9b, 40)
                        if mutation in ('positive-reference','duplicate-unknown-reference'):
                            group += ebml(0xfb, b'\x01' if mutation=='positive-reference' else b'\0')
                        body += ebml(0xa0, group);count += 1
                        continue
                    count += 1
            body += raw[at:e]
        out += ebml(0x1f43b675, body)
    return bytes(out)


def run(worker, report):
    report = Path(report);report.mkdir(parents=True, exist_ok=True);rows = []
    with tempfile.TemporaryDirectory(prefix='fox-vfr-worker-') as t:
        root = Path(t);control = None
        for case in CASES:
            name = case[0];folder = root / name
            source, pixels, frames, duration, ref, profile = media(folder, case)
            raw = source.read_bytes();audio = pcm(source, folder, 'source-pcm')
            video = decoded(source, pixels, folder, 'source-video')
            for unknown in (False, True):
                path = folder / ('unknown.webm' if unknown else 'finite.webm')
                path.write_bytes(unknown_clusters(raw, scan(raw)) if unknown else raw)
                before = hashlib.sha256(path.read_bytes()).hexdigest();partials = []
                def checkpoint(data, event):
                    if not event['mediaReady']:return
                    p = folder / 'partial.webm';p.write_bytes(data)
                    actual = probe(p, folder, 'partial-probe')
                    for kind in ('video', 'audio'):
                        assert packets(actual, kind) == packets(ref, kind)[:len(packets(actual, kind))]
                    sound = pcm(p, folder, 'partial-pcm');assert sound == audio[:len(sound)]
                    partials.append(dict(bytes=len(data), samples=len(sound)//(4*profile[2])))
                row, output = serve(worker, path, mode=MODE, format_hint='webm',
                                    video_samples=frames, video_duration=duration,
                                    audio_samples=profile[5], short_reads=True, on_wait=checkpoint)
                assert row['exitCode'] == 0 and row['result']['code'] == 'data' and row['firstCompleteFragmentBeforeSourceComplete'] and len(partials) >= 3, row
                sealed = folder / 'sealed.webm';sealed.write_bytes(output)
                actual = probe(sealed, folder, 'sealed-probe')
                for kind in ('video', 'audio'):assert packets(actual, kind) == packets(ref, kind)
                assert pcm(sealed, folder, 'sealed-pcm') == audio
                assert decoded(sealed, pixels, folder, 'sealed-video') == video
                assert hashlib.sha256(path.read_bytes()).hexdigest() == before
                row.update(profile=name+('-unknown' if unknown else '-finite'),frames=frames,
                           duration100ns=duration,exactPcm=True,exactPacketsAndClocks=True,
                           decodedFramesIdentical=True,sourceUnchanged=True,partials=partials)
                rows.append(row)
                (report/'progress.json').write_text(json.dumps(dict(count=len(rows))))
                if control is None:control = path,frames,duration,profile[5]
        source,frames,duration,samples = control;raw = source.read_bytes()
        for mutation in ('missing-duration','zero-duration','huge-duration','duplicate-duration',
                         'gap','duplicate','regression','missing-reference','positive-reference',
                         'lost-frame','last-short','duplicate-unknown-reference'):
            target = frames-1 if mutation in ('lost-frame','last-short') else 1
            bad = root/(mutation+'.webm');bad.write_bytes(corrupt(raw,target,mutation))
            row,_ = serve(worker,bad,mode=MODE,format_hint='webm',video_samples=frames,
                          video_duration=duration,audio_samples=samples,growing=False)
            assert row['exitCode']==1 and row['result']['code'].startswith(('unsupported-','incomplete-')), (mutation,row)
            row['profile']='reject-'+mutation;rows.append(row)
        for label,count,end in (('missing-end',frames,None),('zero-end',frames,0),
                                ('non-ms-end',frames,duration+1),('long-end',frames,duration+10000),
                                ('short-end',frames,duration-10000),('short-count',frames-1,duration),
                                ('long-count',frames+1,duration)):
            row,_=serve(worker,source,mode=MODE,format_hint='webm',video_samples=count,
                        video_duration=end,audio_samples=samples,growing=False)
            assert row['exitCode']==1 and row['result']['code']!='data', (label,row)
            row['profile']='reject-'+label;rows.append(row)
        for fault in ('cancel-wait','cancel-write','integrity','unconfirmed-eof','kill-wait'):
            row,_=serve(worker,source,mode=MODE,format_hint='webm',video_samples=frames,
                        video_duration=duration,audio_samples=samples,fault=fault)
            assert row['exitCode']!=0;row['profile']=fault;rows.append(row)
    result=dict(result='PASS',count=len(rows),rows=rows,
                workerSha256=hashlib.sha256(Path(worker).read_bytes()).hexdigest(),ordinaryMainTested=False)
    (report/'result.json').write_text(json.dumps(result,indent=2)+'\n');print('VFR worker PASS',len(rows));return result


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--worker',type=Path,required=True);p.add_argument('--report-directory',required=True)
    a=p.parse_args();run(a.worker.resolve(),a.report_directory)
