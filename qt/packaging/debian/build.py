#!/usr/bin/env python3
"""Build a Mint 22 / Ubuntu 24.04 amd64 Debian package from a clean Qt stage."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--stage', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--version', default='0.2.0-1')
    args = p.parse_args()
    source = args.stage.resolve()
    assert (source / 'foxicmp-qt').is_file()
    assert not any(x.name == 'FoxicMPQt.ini' for x in source.rglob('*')), 'Do not ship a personal profile'
    subprocess.run(['dpkg', '--validate-version', args.version], check=True)
    version = subprocess.check_output([str(source / 'foxicmp-qt'), '--version'],
                                     env={**os.environ, 'QT_QPA_PLATFORM': 'offscreen'}, text=True).strip()
    assert version == 'FoxicMPQt ' + args.version.split('-', 1)[0], version
    args.output.mkdir(parents=True, exist_ok=True)
    output = args.output.resolve() / f'foxiqmp-qt_{args.version}_amd64.deb'
    with tempfile.TemporaryDirectory(prefix='foxiqmp-deb-') as temp:
        work = Path(temp)
        stage = work / 'package'
        payload = stage / 'usr/lib/foxiqmp-qt'
        shutil.copytree(source, payload)
        fingerprint = hashlib.sha256()
        for file in sorted(payload.rglob('*')):
            if file.is_file():
                fingerprint.update(file.relative_to(payload).as_posix().encode())
                fingerprint.update(hashlib.file_digest(file.open('rb'), 'sha256').digest())
        (payload / '.payload-id').write_text(args.version + ':' + fingerprint.hexdigest() + '\n')
        bindir = stage / 'usr/bin'; bindir.mkdir(parents=True)
        launcher = bindir / 'foxiqmp-qt'
        shutil.copyfile(Path(__file__).with_name('launcher.py'), launcher)
        launcher.chmod(0o755)
        desktop = stage / 'usr/share/applications/foxiqmp-qt.desktop'
        desktop.parent.mkdir(parents=True)
        desktop.write_text('''[Desktop Entry]
Type=Application
Name=FoxiqMP qt
GenericName=Media Player
GenericName[ru]=Медиаплеер
Comment=FoxicMP family — Linux Qt
Exec=foxiqmp-qt -- %U
Icon=foxiqmp-qt
Terminal=false
Categories=AudioVideo;Player;Video;
MimeType=video/mp4;video/x-matroska;video/webm;video/x-msvideo;video/mpeg;video/quicktime;audio/mpeg;audio/flac;audio/ogg;audio/x-wav;application/x-mpegURL;
StartupWMClass=FoxicMPQt
''')
        subprocess.run(['desktop-file-validate', str(desktop)], check=True)
        icon = stage / 'usr/share/icons/hicolor/256x256/apps/foxiqmp-qt.png'
        icon.parent.mkdir(parents=True)
        shutil.copyfile(ROOT / 'qt/resources/icon.png', icon)
        docs = stage / 'usr/share/doc/foxiqmp-qt'; docs.mkdir(parents=True)
        shutil.copyfile(ROOT / 'LICENSE.txt', docs / 'copyright')
        shutil.copyfile(ROOT / 'qt/packaging/debian/README.md', docs / 'README.md')
        # Let the host's shlibs database derive ABI requirements of both ELF files.
        debian = work / 'debian'; debian.mkdir()
        (debian / 'control').write_text('Source: foxiqmp-qt\nSection: video\nPriority: optional\nMaintainer: FoxicMP Team <noreply@github.com>\n\nPackage: foxiqmp-qt\nArchitecture: amd64\nDescription: FoxiqMP qt media player\n')
        result = subprocess.run(['dpkg-shlibdeps', '-O', '-e' + str(payload / 'foxicmp-qt'), '-e' + str(payload / 'foxicmp-shader-check')], cwd=work, check=True, text=True, capture_output=True)
        depends = next(line.split('=', 1)[1] for line in result.stdout.splitlines() if line.startswith('shlibs:Depends='))
        control = stage / 'DEBIAN'; control.mkdir()
        size = sum(f.stat().st_size for f in stage.rglob('*') if f.is_file()) // 1024
        (control / 'control').write_text(f'''Package: foxiqmp-qt
Version: {args.version}
Section: video
Priority: optional
Architecture: amd64
Maintainer: FoxicMP Team <noreply@github.com>
Installed-Size: {size}
Depends: {depends}, python3, qt6-qpa-plugins
Recommends: ffmpeg, yt-dlp, mint-meta-codecs
Homepage: https://github.com/LiberVixer/FoxicMP
Description: FoxiqMP qt media player for Linux Mint
 Qt 6/libmpv edition of FoxicMP, with playlists, chapters and subtitles.
 A writable application copy and its portable profile are kept together in
 ~/.local/opt/FoxiqMP-qt. Upgrades and removal preserve personal profiles.
''')
        # Cache refreshes are idempotent and do not assign MIME defaults.
        script = '''#!/bin/sh
set -e
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database -q /usr/share/applications
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true
fi
exit 0
'''
        for name in ('postinst', 'postrm'):
            (control / name).write_text(script); (control / name).chmod(0o755)
        for directory in (d for d in stage.rglob('*') if d.is_dir()):
            directory.chmod(0o755)
        stage.chmod(0o755)
        for file in (f for f in stage.rglob('*') if f.is_file()):
            executable = file in (launcher, payload / 'foxicmp-qt', payload / 'foxicmp-shader-check',
                                  control / 'postinst', control / 'postrm')
            file.chmod(0o755 if executable else 0o644)
        subprocess.run(['dpkg-deb', '--root-owner-group', '--build', str(stage), str(output)], check=True)
    print(output)

if __name__ == '__main__':
    main()
