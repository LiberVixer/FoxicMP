"""Prepare the pinned QtCore patch overlay with Python 3.12+ and GNU patch.

Run on the source host; build_qtcore_long_paths.ps1 consumes the verified inputs
on Windows. No files in the installed toolchain are modified.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import urllib.request


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fetch(url, path, digest):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        pending = path.with_name(path.name + '.download')
        with urllib.request.urlopen(url, timeout=60) as response, pending.open('wb') as stream:
            shutil.copyfileobj(response, stream)
        if sha(pending) != digest:
            raise ValueError('Downloaded source SHA mismatch: ' + url)
        pending.replace(path)
    if sha(path) != digest:
        raise ValueError('Cached source SHA mismatch: ' + str(path))


def prepare(work, output):
    here = Path(__file__).resolve().parent
    pin = json.loads((here/'qt-6.12.0-long-lock-paths.json').read_text())
    work, output = work.resolve(), output.resolve()
    source = work/('qtbase-everywhere-src-' + pin['version'])
    if source.exists() or output.exists():
        raise FileExistsError('Use a fresh work directory and overlay output')
    archive = work/('qtbase-everywhere-src-' + pin['version'] + '.tar.xz')
    fetch(pin['archiveUrl'], archive, pin['archiveSha256'])
    cpp = work/'cppwinrt-3.0.260818.1-3.pkg.tar.zst'
    fetch(pin['cppWinRT']['url'], cpp, pin['cppWinRT']['sha256'])
    with tarfile.open(archive, 'r:xz') as tar:
        tar.extractall(work, filter='data')
    names = set()
    for row in pin['packagePatches']:
        patch = work/'package-patches'/row['name']
        url = ('https://raw.githubusercontent.com/msys2/MINGW-packages/' +
               pin['packageRecipeCommit'] + '/mingw-w64-qt6-base/' + row['name'])
        fetch(url, patch, row['sha256'])
        subprocess.run(['patch', '-p1', '-i', str(patch)], cwd=source, check=True)
        for name in re.findall(r'^\+\+\+\s+(\S+)', patch.read_text(), re.M):
            if name != '/dev/null':
                names.add('/'.join(name.split('/')[1:]))
    patch = here/'qt-6.12.0-long-lock-paths.patch'
    if sha(patch) != pin['localPatchSha256']:
        raise ValueError('Local patch SHA mismatch')
    temp_target = source/'src/corelib/io/qfilesystemengine_win.cpp'
    if sha(temp_target) != pin['tempPathBeforeSha256']:
        raise ValueError('Unexpected upstream temporary-path source')
    target = source/'src/corelib/io/qlockfile_win.cpp'
    if sha(target) != pin['qlockfileBeforeSha256']:
        raise ValueError('Unexpected upstream QLockFile source')
    subprocess.run(['patch', '-p1', '-i', str(patch)], cwd=source, check=True)
    if sha(target) != pin['qlockfileAfterSha256']:
        raise ValueError('Unexpected patched QLockFile source')
    if sha(temp_target) != pin['tempPathAfterSha256']:
        raise ValueError('Unexpected patched temporary-path source')
    names.add('src/corelib/io/qlockfile_win.cpp')
    names.add('src/corelib/io/qfilesystemengine_win.cpp')
    if sorted(names) != pin['overlayFiles']:
        raise ValueError('Package overlay does not match source pin')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(output, 'w') as tar:
        for name in sorted(names):
            path = source/name
            if not path.is_file() or '..' in Path(name).parts:
                raise ValueError('Unsafe package overlay path')
            info = tar.gettarinfo(str(path), arcname=source.name+'/'+name)
            info.uid = info.gid = 0
            info.uname = info.gname = ''
            info.mtime = 0
            info.mode = 0o644
            with path.open('rb') as stream:
                tar.addfile(info, stream)
    if sha(output) != pin['overlaySha256']:
        raise ValueError('Prepared overlay is not reproducible')
    return dict(sourceArchive=str(archive), overlay=str(output),
                cppWinRTArchive=str(cpp), overlaySha256=sha(output))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--overlay', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.work, args.overlay)))
