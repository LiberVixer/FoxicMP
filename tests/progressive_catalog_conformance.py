"""Pure paged segment model: bounds, integrity, identity and append-only history.

No player API, filesystem adapter, media decoder or Harvester integration.
"""
import argparse
import copy
import hashlib
import json
import subprocess


def encode(value):
    return json.dumps(value, ensure_ascii=False, separators=(',', ':'))


def resource(rid):
    return dict(id=rid, name=rid+'.part', length='4',
                ranges=[dict(offset='0', length='4')], state='complete')


def segment(kind, index):
    return dict(resourceId=f'{kind}-{index}', sequence=str(index), start=str(index*60000000),
                duration='60000000', timestampOffset='0', discontinuity=False, configurationId='main')


def fixture(count=160):
    root = dict(schemaVersion=2, sessionId='a'*32, generation='1', revision='1',
                writer=dict(instanceId='b'*32, state='downloading'),
                timeline=dict(timebase=dict(numerator=1, denominator=10000000),
                              live=False, duration='300000000000', timestampOffset='0'),
                resources=[resource(kind+'-init') for kind in ('video', 'audio')], tracks=[],
                segmentCatalog=[])
    for kind, codec in [('video', 'h264'), ('audio', 'aac-lc')]:
        root['tracks'].append(dict(id=kind, kind=kind, required=True, codecHint=codec,
                                   containerHint='fmp4', state='downloading', initializationReady=True,
                                   initializationResourceId=kind+'-init', source=dict(segments=[])))
    pages = {}
    for start in range(0, count, 64):
        page = dict(version=1, sessionId=root['sessionId'], generation='1',
                    writerInstanceId=root['writer']['instanceId'], pageId='page-'+str(start//64),
                    resources=[], tracks=[])
        by_track = dict(video=[], audio=[])
        for i in range(start, min(start+64, count)):
            kind = 'audio' if i % 2 else 'video'
            part = segment(kind, i//2)
            by_track[kind].append(part)
            page['resources'].append(resource(part['resourceId']))
        page['tracks'] = [dict(trackId=kind, segments=parts) for kind, parts in by_track.items() if parts]
        replace_page(root, pages, page)
    return root, pages


def replace_page(root, pages, page, raw=None):
    name = page['pageId']+'.json'
    text = encode(page) if raw is None else raw
    descriptor = dict(id=page['pageId'], name=name, length=str(len(text.encode('utf-8'))),
                      sha256=hashlib.sha256(text.encode('utf-8')).hexdigest())
    for i, current in enumerate(root['segmentCatalog']):
        if current['id'] == page['pageId']:
            root['segmentCatalog'][i] = descriptor
            break
    else:
        root['segmentCatalog'].append(descriptor)
    pages[name] = text


def run(runner):
    checks = []
    process = subprocess.Popen([runner], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True, encoding='utf-8')
    def rpc(action, root=None, pages=None):
        command = dict(action=action)
        if root is not None:
            command.update(session=root, pages={} if pages is None else pages)
        process.stdin.write(encode(command)+'\n')
        process.stdin.flush()
        line = process.stdout.readline()
        assert line, process.stderr.read()
        return json.loads(line)
    def case(name, function):
        function()
        checks.append(name)
    def valid(count):
        root, pages = fixture(count)
        result = rpc('decode', root, pages)
        assert result['ok'] and result['resources'] == count+2, result
        assert sum(result['segments'].values()) == count, result
        assert result['loads'] == len(pages), result
    def reject(change, code=None, no_load=False):
        root, pages = fixture()
        change(root, pages)
        result = rpc('decode', root, pages)
        assert not result['ok'], result
        if code:
            assert result['code'] == code, result
        if no_load:
            assert result['loads'] == 0, result
    def page_change(root, pages, mutate, raw=None):
        page = json.loads(pages['page-0.json'])
        mutate(page)
        replace_page(root, pages, page, raw=raw)
    try:
        for count in (0, 64, 160, 8190):
            case(f'decode-{count}-segments-with-bounded-pages', lambda count=count: valid(count))
        def inline_compat():
            root, pages = fixture(0)
            root.pop('segmentCatalog')
            answer = rpc('decode', root, pages)
            assert answer['ok'] and answer['loads'] == 0, answer
            assert rpc('inline', root)['ok']
        case('legacy-inline-snapshot-still-valid', inline_compat)
        def old_parser():
            assert not rpc('inline', fixture()[0])['ok']
        case('old-public-parser-does-not-enable-catalog', old_parser)
        case('missing-index-is-explicit-waiting', lambda: reject(lambda r, p: p.clear(), 'waiting-index'))
        case('digest-mismatch', lambda: reject(lambda r, p: p.update({'page-0.json': p['page-0.json'].replace('main', 'xxxx', 1)}), 'integrity-mismatch'))
        case('length-mismatch', lambda: reject(lambda r, p: p.update({'page-0.json': p['page-0.json']+' '}), 'catalog_length_mismatch'))
        for field, value in [('name', '../page.json'), ('name', 'CON.json'), ('length', '65537'),
                             ('length', 3), ('length', '01'), ('sha256', 'A'*64), ('sha256', '0'*63)]:
            case('reject-descriptor-'+field+'-'+str(value), lambda field=field, value=value:
                 reject(lambda r, p: r['segmentCatalog'][-1].update({field: value}), no_load=True))
        case('duplicate-page-id-before-loading', lambda: reject(lambda r, p: r['segmentCatalog'].append(copy.deepcopy(r['segmentCatalog'][0])), no_load=True))
        case('case-insensitive-page-filename-collision', lambda: reject(lambda r, p: r['segmentCatalog'][1].update(name='PAGE-0.JSON'), no_load=True))
        def too_many_pages(r, p):
            descriptor = r['segmentCatalog'][0]
            r['segmentCatalog'] = [dict(descriptor, id='p'+str(i), name='p'+str(i)+'.json') for i in range(129)]
        case('catalog-page-count-bound-before-loading', lambda: reject(too_many_pages, no_load=True))
        for field, value in [('sessionId', 'c'*32), ('generation', '2'), ('writerInstanceId', 'c'*32)]:
            case('reject-page-'+field+'-identity', lambda field=field, value=value:
                 reject(lambda r, p: page_change(r, p, lambda page: page.update({field: value})), 'catalog_identity_mismatch'))
        for name, mutate in [
            ('unknown-field', lambda p: p.update(unexpected=True)),
            ('version', lambda p: p.update(version=2)),
            ('unknown-track', lambda p: p['tracks'][0].update(trackId='unknown')),
            ('duplicate-track', lambda p: p['tracks'].append(copy.deepcopy(p['tracks'][0]))),
            ('unreferenced-resource', lambda p: p['tracks'][0]['segments'].pop()),
            ('cross-page-resource', lambda p: p['tracks'][0]['segments'][0].update(resourceId='video-64')),
            ('duplicate-segment-resource', lambda p: p['tracks'][0]['segments'][1].update(resourceId='video-0')),
            ('incomplete-resource', lambda p: p['resources'][0].update(state='downloading')),
            ('unknown-length', lambda p: p['resources'][0].update(length=None)),
            ('unconfirmed-tail', lambda p: p['resources'][0].update(ranges=[dict(offset='0', length='3')])),
            ('retired-resource', lambda p: p['resources'][0].update(retired=True)),
            ('float-sequence', lambda p: p['tracks'][0]['segments'][0].update(sequence=0)),
            ('overlapping-timeline', lambda p: p['tracks'][0]['segments'][1].update(start='0')),
            ('path-traversal-resource', lambda p: p['resources'][0].update(name='../media.part')),
            ('page-as-media', lambda p: p['resources'][0].update(name='PAGE-0.JSON')),
        ]:
            case('reject-page-'+name, lambda mutate=mutate: reject(lambda r, p: page_change(r, p, mutate)))
        def duplicate_keys(r, p):
            page = json.loads(p['page-0.json'])
            raw = p['page-0.json'].replace('"version":1', '"version":1,"version":1', 1)
            replace_page(r, p, page, raw=raw)
        case('duplicate-json-key', lambda: reject(duplicate_keys))
        def invalid_utf8(r, p):
            # JSON string transport produces UTF-8; an unpaired surrogate inside
            # the page's JSON string must still be rejected by the page decoder.
            page = json.loads(p['page-0.json'])
            raw = p['page-0.json'].replace('main', r'\ud800', 1)
            replace_page(r, p, page, raw=raw)
        case('invalid-page-unicode', lambda: reject(invalid_utf8))
        def null_payload(r, p):
            page = json.loads(p['page-0.json'])
            replace_page(r, p, page, raw=p['page-0.json']+'\0ignored')
        case('embedded-null-cannot-hide-trailing-payload', lambda: reject(null_payload))
        def maximum_page():
            root, pages = fixture(64)
            page = json.loads(pages['page-0.json'])
            raw = pages['page-0.json']
            raw += ' '*(65536-len(raw.encode('utf-8')))
            replace_page(root, pages, page, raw=raw)
            answer = rpc('decode', root, pages)
            assert answer['ok'] and answer['resources'] == 66, answer
        case('exact-64-KiB-page-boundary', maximum_page)
        def exact_times():
            root, pages = fixture(2)
            page = json.loads(pages['page-0.json'])
            for t in page['tracks']:
                t['segments'][0].update(start='9007199254740993', duration='7')
            root['timeline']['duration'] = '9007199254741000'
            replace_page(root, pages, page)
            answer = rpc('decode', root, pages)
            assert answer['ok'] and set(answer['firstStarts'].values()) == {'9007199254740993'}, answer
        case('timestamps-above-javascript-exact-number-boundary', exact_times)
        def inline_segments(r, p):
            r['tracks'][0]['source']['segments'] = [segment('video', i) for i in range(129)]
        case('inline-track-keeps-128-segment-bound', lambda: reject(inline_segments, no_load=True))
        def resource_limit():
            root, pages = fixture(8191)
            answer = rpc('decode', root, pages)
            assert not answer['ok'] and answer['path'] == 'page.resources', answer
        case('expanded-resource-limit-8192-includes-init', resource_limit)
        def segment_limit():
            root, pages = fixture(4097)
            number = 0
            for descriptor in list(root['segmentCatalog']):
                page = json.loads(pages[descriptor['name']])
                parts = []
                for r in page['resources']:
                    part = segment('video', number)
                    part['resourceId'] = r['id']
                    parts.append(part)
                    number += 1
                page['tracks'] = [dict(trackId='video', segments=parts)]
                replace_page(root, pages, page)
            answer = rpc('decode', root, pages)
            assert not answer['ok'] and answer['path'] == 'page.tracks[].segments', answer
        case('expanded-track-segment-limit-4096', segment_limit)
        def append():
            root, pages = fixture(64)
            assert rpc('decode', root, pages)['ok']
            next_root, next_pages = fixture(128)
            next_root['revision'] = '2'
            result = rpc('advance', next_root, next_pages)
            assert result['ok'] and result['resources'] == 130, result
        case('append-pages-preserves-published-prefix', append)
        def immutability(kind):
            root, pages = fixture(160)
            assert rpc('decode', root, pages)['ok']
            root['revision'] = '2'
            if kind == 'rewrite':
                page = json.loads(pages['page-0.json'])
                replace_page(root, pages, page, raw=pages['page-0.json']+' ')
            elif kind == 'reorder':
                root['segmentCatalog'][0], root['segmentCatalog'][1] = root['segmentCatalog'][1], root['segmentCatalog'][0]
            else:
                descriptor = root['segmentCatalog'].pop()
                page = json.loads(pages[descriptor['name']])
                root['resources'].extend(page['resources'])
                for archived_track in page['tracks']:
                    target = next(t for t in root['tracks'] if t['id'] == archived_track['trackId'])
                    target['source']['segments'].extend(archived_track['segments'])
            result = rpc('advance', root, pages)
            assert not result['ok'], result
            if kind != 'reorder':
                assert result['path'] == '$.segmentCatalog', result
            status = rpc('status')
            assert status['revision'] == '1' and status['resources'] == 162, status
        for kind in ('rewrite', 'reorder', 'remove'):
            case('reject-published-page-'+kind+'-without-mutating-active-model', lambda kind=kind: immutability(kind))
        def archive_tail():
            root, pages = fixture(64)
            page = json.loads(pages.pop('page-0.json'))
            root['segmentCatalog'] = []
            root['resources'].extend(page['resources'][:60])
            # The remaining four resources/segments are not published yet.
            published = {r['id'] for r in root['resources']}
            for t in page['tracks']:
                next(t2 for t2 in root['tracks'] if t2['id'] == t['trackId'])['source']['segments'] = [s for s in t['segments'] if s['resourceId'] in published]
            assert rpc('decode', root, pages)['ok']
            following, following_pages = fixture(64)
            following['revision'] = '2'
            answer = rpc('advance', following, following_pages)
            assert answer['ok'] and answer['resources'] == 66, answer
        case('move-published-inline-tail-to-immutable-page', archive_tail)
        def inline_limit():
            root, pages = fixture(0)
            root['resources'].extend(resource('extra-'+str(i)) for i in range(63))
            answer = rpc('decode', root, pages)
            assert not answer['ok'] and answer['loads'] == 0, answer
        case('root-keeps-64-inline-resource-bound', inline_limit)
    finally:
        process.stdin.close()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            raise
        error = process.stderr.read()
        process.stdout.close()
        process.stderr.close()
        assert process.returncode == 0, error
    return dict(result='PASS', count=len(checks), checks=checks, scope='pure-model-only',
                maxTestedResources=8192, publicPlaybackCapability=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--runner', required=True)
    args = parser.parse_args()
    print(encode(run(args.runner)))
