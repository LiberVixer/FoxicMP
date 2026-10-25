#!/usr/bin/python3
"""Materialize the .deb payload as a writable portable application for this user."""
import fcntl
import os
from pathlib import Path
import shutil
import sys
import tempfile

# Installed as /usr/bin/foxiqmp-qt. Relative discovery also permits testing a
# fully extracted .deb without writing to the system's /usr or installing it.
PAYLOAD = Path(__file__).resolve().parents[1] / 'lib/foxiqmp-qt'

def stage_payload(source, destination):
    """Replace shipped files atomically; never remove or copy user profiles."""
    destination.mkdir(parents=True, exist_ok=True)
    stamp = (source / '.payload-id').read_bytes()
    with (destination / '.package-update.lock').open('a+b') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        marker = destination / '.payload-id'
        if marker.exists() and marker.read_bytes() == stamp:
            return
        for original in sorted(source.rglob('*')):
            if not original.is_file() or original.name == '.payload-id':
                continue
            relative = original.relative_to(source)
            if original.name == 'FoxicMPQt.ini':
                raise RuntimeError('A Debian payload must not contain a personal profile')
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            fd, temp = tempfile.mkstemp(prefix='.package-', dir=target.parent)
            try:
                with os.fdopen(fd, 'wb') as stream, original.open('rb') as reader:
                    shutil.copyfileobj(reader, stream)
                os.chmod(temp, original.stat().st_mode & 0o777)
                os.replace(temp, target)
            finally:
                Path(temp).unlink(missing_ok=True)
        fd, temp = tempfile.mkstemp(prefix='.package-', dir=destination)
        try:
            with os.fdopen(fd, 'wb') as stream:
                stream.write(stamp)
            os.replace(temp, marker)
        finally:
            Path(temp).unlink(missing_ok=True)


def main():
    home = Path.home()
    destination = home / '.local/opt/FoxiqMP-qt'
    stage_payload(PAYLOAD, destination)
    executable = destination / 'foxicmp-qt'
    os.execv(str(executable), [str(executable), *sys.argv[1:]])

if __name__ == '__main__':
    main()
