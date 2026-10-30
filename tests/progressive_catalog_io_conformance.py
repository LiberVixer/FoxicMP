"""Real local catalog I/O; no media backend, UI, profiles or Harvester."""
import argparse
import copy
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

from progressive_catalog_conformance import fixture, replace_page


class Fixture:
    def __init__(self, runner, root):
        self.root = root
        self.data = root/'Данные'
        self.data.mkdir()
        self.process = subprocess.Popen([runner], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, text=True, encoding='utf-8')

    def rpc(self, action, **fields):
        self.process.stdin.write(json.dumps(dict(action=action, **fields))+'\n')
        self.process.stdin.flush()
        line = self.process.stdout.readline()
        assert line, self.process.stderr.read()
        return json.loads(line)

    def open(self, path=None):
        answer = self.rpc('open', directory=str(path or self.data))
        assert answer['ok'], answer

    def write(self, pages):
        for name, text in pages.items():
            (self.data/name).write_bytes(text.encode('utf-8'))

    def publish(self, value, **fields):
        return self.rpc('publish', session=value, **fields)

    def accepted(self, value):
        answer = self.publish(value)
        assert answer['ok'], answer

    def pending_result(self):
        deadline = time.monotonic()+10
        while time.monotonic() < deadline:
            value = self.rpc('result')
            if not value.get('pending'):
                return value
            time.sleep(.01)
        raise AssertionError('Catalog publish did not finish')

    def close(self):
        self.process.stdin.close()
        try:
            self.process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
            raise
        error = self.process.stderr.read()
        self.process.stdout.close()
        self.process.stderr.close()
        assert self.process.returncode == 0, error


def run(runner):
    checks, skipped = [], []

    def case(name, check):
        with tempfile.TemporaryDirectory(prefix='foxicmp-catalog-io-') as directory:
            f = Fixture(runner, Path(directory))
            try:
                reason = check(f)
                if reason:
                    skipped.append(dict(name=name, reason=reason))
                else:
                    checks.append(name)
            finally:
                f.close()

    def valid(f, count):
        value, pages = fixture(count)
        f.write(pages)
        f.open()
        f.accepted(value)
        assert f.rpc('status') == dict(ok=True, resources=count+2, pages=len(pages), revision='1')
        # Only page files exist. Successful index reading must not open media.
        assert sorted(p.name for p in f.data.iterdir()) == sorted(pages)

    case('local-160-segments-without-opening-media', lambda f: valid(f, 160))
    case('local-catalog-8192-resources', lambda f: valid(f, 8190))

    def missing(f, append=False):
        value, pages = fixture(64 if append else 160)
        f.open()
        if append:
            f.write(pages)
            f.accepted(value)
            value, pages = fixture(128)
            value['revision'] = '2'
        else:
            f.write({name: text for name, text in pages.items() if name != 'page-1.json'})
        answer = f.publish(value)
        assert not answer['ok'] and answer['code'] == 'waiting-index', answer
        if append:
            assert f.rpc('status')['revision'] == '1'
        else:
            assert f.rpc('status')['code'] == 'waiting-index'
        f.write(pages)
        f.accepted(value)
        assert f.rpc('status')['revision'] == value['revision']

    case('missing-page-is-waiting-and-can-be-retried', missing)
    case('missing-appended-page-retains-accepted-model', lambda f: missing(f, True))

    def corrupted(f, mode):
        value, pages = fixture(64)
        f.write(pages)
        path = f.data/'page-0.json'
        data = path.read_bytes()
        if mode == 'short':
            path.write_bytes(data[:-1])
        elif mode == 'long':
            path.write_bytes(data+b' ')
        elif mode == 'oversized':
            path.write_bytes(b'X'*65537)
        else:
            path.write_bytes(data.replace(b'main', b'xxxx', 1))
        f.open()
        answer = f.publish(value)
        code = 'integrity-mismatch' if mode == 'digest' else 'catalog_length_mismatch'
        assert not answer['ok'] and answer['code'] == code, answer

    for mode in ('short', 'long', 'oversized', 'digest'):
        case('reject-physical-page-'+mode, lambda f, mode=mode: corrupted(f, mode))

    def maximum(f):
        value, pages = fixture(64)
        page = json.loads(pages['page-0.json'])
        raw = pages['page-0.json']
        replace_page(value, pages, page, raw=raw+' '*(65536-len(raw.encode('utf-8'))))
        f.write(pages)
        f.open()
        f.accepted(value)

    case('read-exact-64-KiB-page-in-bounded-chunks', maximum)

    def replace(f, identical):
        value, pages = fixture(64)
        f.write(pages)
        f.open()
        f.accepted(value)
        path = f.data/'page-0.json'
        if identical:
            replacement = f.data/'replacement.json'
            replacement.write_bytes(path.read_bytes())
            os.replace(replacement, path)
        else:
            with path.open('r+b') as stream:
                data = stream.read().replace(b'main', b'xxxx', 1)
                stream.seek(0)
                stream.write(data)
        value['revision'] = '2'
        answer = f.publish(value)
        expected = 'source-replaced' if identical else 'integrity-mismatch'
        assert not answer['ok'] and answer['code'] == expected, answer
        assert f.rpc('status')['revision'] == '1'

    case('reopening-detects-identity-replacement-even-with-identical-bytes', lambda f: replace(f, True))
    case('reopening-detects-in-place-content-rewrite', lambda f: replace(f, False))

    def removed(f):
        value, pages = fixture(64)
        f.write(pages)
        f.open()
        f.accepted(value)
        (f.data/'page-0.json').unlink()
        value['revision'] = '2'
        answer = f.publish(value)
        assert not answer['ok'] and answer['code'] == 'missing-index', answer
        assert f.rpc('status')['revision'] == '1'

    case('published-page-disappearance-is-error-not-waiting', removed)

    def recover(f):
        value, pages = fixture(64)
        f.write(pages)
        f.open()
        f.accepted(value)
        next_value, next_pages = fixture(128)
        next_value['revision'] = '2'
        f.write(next_pages)
        path = f.data/'page-1.json'
        path.write_bytes(path.read_bytes().replace(b'main', b'xxxx', 1))
        assert f.publish(next_value)['code'] == 'integrity-mismatch'
        assert f.rpc('status')['revision'] == '1'
        replacement = f.data/'replacement.json'
        replacement.write_bytes(next_pages['page-1.json'].encode('utf-8'))
        os.replace(replacement, path)
        f.accepted(next_value)
        assert f.rpc('status')['resources'] == 130

    case('failed-update-does-not-commit-new-page-bindings', recover)

    def handles(f):
        value, pages = fixture(160)
        f.write(pages)
        # Windows security/overlapped-I/O APIs initialize process-wide service
        # handles on their first use. Warm the complete path, then measure
        # repeated owners and publications against the resulting steady state.
        f.open()
        f.accepted(value)
        f.rpc('release')
        baseline = f.rpc('handles')['handles']
        f.open()
        held = f.rpc('handles')['handles']
        assert held <= baseline+2, (baseline, held)
        for revision in range(1, 31):
            value['revision'] = str(revision)
            f.accepted(value)
            assert f.rpc('handles')['handles'] <= held+1
        f.rpc('release')
        assert f.rpc('handles')['handles'] <= baseline+1
        for _ in range(15):
            f.open()
            f.accepted(value)
            f.rpc('release')
            assert f.rpc('handles')['handles'] <= baseline+1
        for name in pages:
            (f.data/name).unlink()

    case('repeated-publish-closes-page-handles-and-owner-closes-directory', handles)

    def cancelled(f, during=False):
        value, pages = fixture(64)
        f.write(pages)
        f.open()
        f.accepted(value)
        if during:
            value, pages = fixture(8190)
            value['revision'] = '2'
            f.write(pages)
            assert f.rpc('beginPublish', session=value)['ok']
        f.rpc('cancel')
        answer = f.pending_result() if during else f.publish(value)
        assert not answer['ok'] and answer['code'] == 'cancelled', answer
        assert f.rpc('status')['revision'] == '1'
        assert f.publish(value)['code'] == 'cancelled'

    case('cancel-before-read-retains-accepted-model', cancelled)
    case('cancel-during-large-catalog-retains-accepted-model', lambda f: cancelled(f, True))

    def link(f, mode):
        value, pages = fixture(64)
        f.write(pages)
        path = f.data/'page-0.json'
        outside = f.root/'outside.json'
        path.rename(outside)
        try:
            if mode == 'hardlink':
                os.link(outside, path)
            else:
                path.symlink_to(outside)
        except OSError as error:
            if mode == 'symlink' and os.name == 'nt':
                return str(error)
            raise
        f.open()
        answer = f.publish(value)
        assert not answer['ok'] and answer['code'] == 'unsafe-resource', answer

    case('reject-hard-linked-page', lambda f: link(f, 'hardlink'))
    case('reject-symbolic-linked-page', lambda f: link(f, 'symlink'))

    def directory_page(f):
        value, pages = fixture(64)
        (f.data/'page-0.json').mkdir()
        f.open()
        answer = f.publish(value)
        # Windows refuses the non-directory CreateFile before Inspect can
        # classify this object; Linux opens it and rejects its type in Inspect.
        expected = 'resource-io-error' if os.name == 'nt' else 'unsafe-resource'
        assert not answer['ok'] and answer['code'] == expected, answer

    case('reject-directory-instead-of-page', directory_page)

    def root_link(f, intermediate=False):
        alias = f.root/'alias'
        try:
            alias.symlink_to(f.data, target_is_directory=True)
        except OSError as error:
            if os.name == 'nt':
                result = subprocess.run(['cmd.exe', '/d', '/c', 'mklink', '/J', str(alias), str(f.data)], capture_output=True)
                assert result.returncode == 0, (result.stdout, result.stderr, str(error))
            else:
                raise
        if intermediate:
            (f.data/'child').mkdir()
        answer = f.rpc('open', directory=str(alias/'child' if intermediate else alias))
        assert not answer['ok'] and answer['code'] == 'unsafe-resource', answer

    case('reject-linked-or-reparse-root-directory', root_link)
    case('reject-linked-or-reparse-ancestor-directory', lambda f: root_link(f, True))

    def root_rename(f):
        value, pages = fixture(64)
        f.write(pages)
        f.open()
        f.accepted(value)
        old = f.data.with_name('old-data')
        try:
            f.data.rename(old)
        except OSError:
            if os.name != 'nt':
                raise
            # Windows pins its directory without FILE_SHARE_DELETE.
            value['revision'] = '2'
            f.accepted(value)
            return
        f.data.mkdir()
        f.write({name: text.replace('main', 'xxxx', 1) for name, text in pages.items()})
        value['revision'] = '2'
        answer = f.publish(value)
        if os.name == 'nt':
            assert not answer['ok'] and answer['code'] == 'unsafe-resource', answer
        else:
            assert answer['ok'], answer  # Reads the original pinned directory.

    case('directory-rename-cannot-retarget-the-reader', root_rename)

    def bad_name(f, name):
        value, pages = fixture(64)
        value['segmentCatalog'][0]['name'] = name
        f.open()
        answer = f.publish(value)
        assert not answer['ok'] and answer['code'] == 'invalid_snapshot', answer

    case('reject-traversal-before-opening-page', lambda f: bad_name(f, '../outside.json'))
    case('reject-absolute-path-before-opening-page', lambda f: bad_name(f, str(f.root/'outside.json')))

    if os.name != 'nt':
        def fifo(f):
            value, _ = fixture(64)
            os.mkfifo(f.data/'page-0.json')
            f.open()
            start = time.monotonic()
            answer = f.publish(value)
            assert not answer['ok'] and answer['code'] == 'unsafe-resource', answer
            assert time.monotonic()-start < 1
        case('fifo-page-is-rejected-without-blocking', fifo)
    return dict(result='PASS', scope='local-page-io-only', count=len(checks), checks=checks,
                skipped=skipped, platform=os.name, maxTestedResources=8192,
                publicPlaybackCapability=False, readerLeasesTested=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--runner', required=True)
    args = parser.parse_args()
    print(json.dumps(run(args.runner), ensure_ascii=False))
