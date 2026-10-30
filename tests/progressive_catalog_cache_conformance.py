"""Internal bounded ResourceSource mode: real bytes, LRU and reader ownership."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import tempfile

from progressive_resource_conformance import Fixture, snapshot


def model(count=200):
    value = snapshot(ranges=((0, 16),))
    value['resources'] = []
    for i in range(count):
        data = payload(i)
        value['resources'].append(dict(id='r'+str(i), name='r'+str(i)+'.part', length='16',
            state='complete', ranges=[dict(offset='0', length='16')],
            integrity=dict(algorithm='sha256', blocks=[dict(offset='0', length='16', sha256=hashlib.sha256(data).hexdigest())])))
    value['tracks'][0]['source'] = dict(resourceId='r0')
    return value


def payload(i):
    return f'{i:016d}'.encode('ascii')


def run(runner):
    checks = []
    def case(name, check):
        with tempfile.TemporaryDirectory(prefix='foxicmp-catalog-cache-') as directory:
            f = Fixture(runner, Path(directory))
            try:
                check(f)
                checks.append(name)
            finally:
                f.close()
    def open_(f, value, write_count=None):
        for r in value['resources'][:write_count]:
            i = int(r['id'][1:])
            (f.data/r['name']).write_bytes(payload(i))
        reply = f.rpc('openCatalog', directory=str(f.data), session=value)
        assert reply['ok'], reply
    def read(f, i, offset=0, count=16):
        return f.rpc('read', resourceId='r'+str(i), offset=str(offset), length=str(count))
    def good(f, i):
        answer = read(f, i)
        assert answer['code'] == 'data' and answer['hex'] == payload(i).hex(), answer
    def readers(f, value):
        result, after = [], ''
        while True:
            page = f.rpc('readers', version=1, sessionId=value['sessionId'], generation=value['generation'],
                         **({'afterReaderId': after} if after else {}))
            assert page['ok'], page
            result.extend(page['readers'])
            after = page.get('nextReaderId', '')
            if not after:
                return result
    def bounded(f):
        value = model()
        open_(f, value)
        first, steady = None, None
        for i in range(200):
            good(f, i)
            ids = f.rpc('bindings')['readerIds']
            assert len(ids) == min(i+1, 8), (i, ids)
            if i == 0:
                first = ids[0]
                premature = f.rpc('readerForget', version=1, readerId=first)
                assert not premature['ok'] and premature['code'] == 'reader_in_use', premature
            if i % 17 == 0:
                receipt = readers(f, value)
                assert len(receipt) <= 8 and not any(r['released'] for r in receipt), receipt
            if i == 7:
                steady = f.rpc('handles')['handles']
            elif i > 7:
                handles = f.rpc('handles')['handles']
                assert handles <= steady+2, (i, steady, handles)
        assert not f.rpc('readerStatus', version=1, readerId=first)['ok']
        for i in (0, 199, 1, 198, 2, 197):
            good(f, i)
        assert len(readers(f, value)) == 8
    case('200-resources-exceed-reader-lifetime-cap-with-only-eight-active-leases', bounded)
    def lru(f):
        value = model(10)
        open_(f, value)
        for i in range(8):
            good(f, i)
        before = {r['resources'][0]['id']: r['readerId'] for r in readers(f, value)}
        good(f, 0)
        good(f, 8)
        after = {r['resources'][0]['id']: r['readerId'] for r in readers(f, value)}
        assert after['r0'] == before['r0'] and 'r1' not in after and 'r8' in after
        good(f, 1)
        final = {r['resources'][0]['id']: r['readerId'] for r in readers(f, value)}
        assert final['r1'] != before['r1'] and 'r2' not in final
    case('least-recently-used-reader-evicted-and-reopened-with-new-receipt', lru)
    def large(f):
        value = model(8192)
        open_(f, value, 1)
        (f.data/'r8191.part').write_bytes(payload(8191))
        good(f, 0)
        good(f, 8191)
        assert len(readers(f, value)) == 2
    case('8192-resource-metadata-opens-only-requested-files', large)
    def changed(f, replace):
        value = model(10)
        open_(f, value)
        good(f, 0)
        for i in range(1, 9):
            good(f, i)
        path = f.data/'r0.part'
        if replace:
            replacement = f.data/'replacement.part'
            replacement.write_bytes(payload(0))
            replacement.replace(path)
        else:
            path.write_bytes(b'X'*16)
        result = read(f, 0)
        expected = 'source-replaced' if replace else 'integrity-mismatch'
        assert result['code'] == expected and result['bytesRead'] == 0 and not result['hex'], result
        assert read(f, 8)['code'] == expected  # Error persists for required media.
    case('evicted-file-identity-replacement-is-persistent-error', lambda f: changed(f, True))
    case('evicted-file-in-place-rewrite-rehashes-before-returning-bytes', lambda f: changed(f, False))
    def missing(f):
        value = model(10)
        open_(f, value)
        for i in range(9):
            good(f, i)
        (f.data/'r0.part').unlink()
        assert read(f, 0)['code'] == 'missing-resource'
    case('evicted-complete-file-disappearance-is-not-waiting-or-eof', missing)
    def truncate(f):
        value = model(10)
        open_(f, value)
        for i in range(9):
            good(f, i)
        with (f.data/'r0.part').open('r+b') as stream:
            stream.truncate(8)
        assert read(f, 0)['code'] == 'source-truncated'
    case('evicted-file-truncation-is-detected-on-reopen', truncate)
    def required(f):
        value = model(1)
        value['resources'][0].pop('integrity')
        result = f.rpc('openCatalog', directory=str(f.data), session=value)
        assert not result['ok'] and result['code'] == 'integrity-required', result
        # The established inline mode still accepts this snapshot.
        result = f.rpc('open', directory=str(f.data), session=value)
        assert result['ok'], result
    case('catalog-requires-digests-without-changing-inline-compatibility', required)
    def reject(f, mode):
        value = model(1)
        if mode == 'length':
            value['resources'][0]['length'] = str(16*1024*1024+1)
            value['resources'][0]['state'] = 'downloading'
        elif mode == 'unknown-extent':
            r = value['resources'][0]
            r.update(length=None, state='downloading')
            r['ranges'][0]['offset'] = str(16*1024*1024)
            r['integrity']['blocks'][0]['offset'] = str(16*1024*1024)
        else:
            value['resources'][0]['retired'] = True
        reply = f.rpc('openCatalog', directory=str(f.data), session=value)
        assert not reply['ok'] and reply['code'] == 'unsupported_media', reply
    case('catalog-resource-size-bound', lambda f: reject(f, 'length'))
    case('catalog-unknown-length-confirmed-extent-bound', lambda f: reject(f, 'unknown-extent'))
    case('catalog-retired-resources-require-another-lifecycle', lambda f: reject(f, 'retired'))
    def unchanged_inline(f):
        value = model(16)
        for i in range(16):
            (f.data/('r'+str(i)+'.part')).write_bytes(payload(i))
        assert f.rpc('open', directory=str(f.data), session=value)['ok']
        for i in range(16):
            good(f, i)
        assert len(f.rpc('bindings')['readerIds']) == 16
        assert len(readers(f, value)) == 16
    case('established-inline-mode-still-pins-all-read-resources', unchanged_inline)
    def lifetime(f):
        value = model(20)
        open_(f, value)
        for i in range(20):
            good(f, i)
        f.rpc('retain')
        f.rpc('release')
        assert not f.rpc('released')['released']
        held = readers(f, value)
        assert len(held) == 8 and not any(r['released'] for r in held)
        f.rpc('dropRetained')
        assert f.rpc('released')['released']
        closed = readers(f, value)
        assert len(closed) == 8 and all(r['released'] and r['filesClosed'] for r in closed)
        for item in closed:
            assert f.rpc('readerForget', version=1, readerId=item['readerId'])['ok']
        assert not readers(f, value)
    case('last-owner-closes-all-files-before-release-receipts', lifetime)
    def publication(f):
        value = model(10)
        open_(f, value)
        for i in range(9):
            good(f, i)
        next_value = model(20)
        next_value['revision'] = '2'
        for i in range(10, 20):
            (f.data/('r'+str(i)+'.part')).write_bytes(payload(i))
        result = f.rpc('publish', session=next_value)
        assert result['ok'], result
        good(f, 19)
        good(f, 0)
        invalid = copy.deepcopy(next_value)
        invalid['revision'] = '3'
        invalid['resources'][0]['integrity']['blocks'][0]['sha256'] = '0'*64
        assert not f.rpc('publish', session=invalid)['ok']
        good(f, 0)
    case('catalog-update-appends-resources-and-invalid-update-preserves-current-source', publication)
    def bounds(f):
        value = model(10)
        open_(f, value)
        good(f, 0)
        assert read(f, 0, 16, 1)['code'] == 'eof'
        assert read(f, 0, 17, 1)['code'] == 'out-of-range'
        oversized = read(f, 0, 0, 65537)
        assert not oversized['ok'] and oversized['code'] == 'invalid_snapshot', oversized
        f.rpc('cancel')
        assert read(f, 0)['code'] == 'cancelled'
    case('bounded-read-eof-offset-and-cancellation-contracts', bounds)
    return dict(result='PASS', scope='internal-catalog-cache-only', count=len(checks), checks=checks,
                maxOpenResources=8, sequentialResourcesTested=200, maxMetadataResourcesTested=8192,
                readerLeasesTested=True, publicPlaybackCapability=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--runner', required=True)
    args = parser.parse_args()
    print(json.dumps(run(args.runner)))
