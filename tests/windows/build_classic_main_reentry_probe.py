"""Build a private main-window reentry copy from coherent Classic MSVC objects.

Run from an x64 Developer Command Prompt. Never use an installed output folder.
No shipping project defines FOXICMP_PRIVATE_CLASSIC_SEEK_TEST.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def read_rsp(path):
    raw = path.read_bytes()
    return raw.decode('utf-16' if raw.startswith(b'\xff\xfe') else 'utf-8-sig')


def build(args):
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    objects = args.normal_objects.resolve()
    normal_player = args.normal_player.resolve()
    for path in objects.glob('*.obj'):
        shutil.copyfile(path, output / path.name)
    receipts = {}
    def command(name, values):
        with (output / (name + '.log')).open('wb') as log:
            result = subprocess.run(values, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
        receipts[name] = result.returncode
        (output / (name + '.exit')).write_text(str(result.returncode) + '\n')
        if result.returncode:
            raise RuntimeError((name, result.returncode, str(output / (name + '.log'))))
    for name, response, source in (('main', args.main_rsp, 'MainFrm'),
                                    ('api', args.api_rsp, 'ClassicProgressiveApi')):
        text = read_rsp(response)
        text = re.sub(r'/Fo"[^"]*"', lambda _: '/Fo"' + str(output / (source + '.obj')) + '"', text)
        text = re.sub(r'/Fd"[^"]*"', lambda _: '/Fd"' + str(output / (name + '.pdb')) + '"', text)
        # /Y- prevents a shipping PCH from hiding the private macro definitions.
        text += ' /Y- /DFOXICMP_PRIVATE_CLASSIC_SEEK_TEST=1'
        rsp = output / (name + '.rsp')
        rsp.write_text(text, encoding='utf-16')
        command(name, ['cl.exe', '@' + str(rsp)])
    flags = ['/nologo', '/std:c++17', '/EHsc', '/utf-8', '/O2', '/MT',
             '/D_UNICODE', '/DUNICODE', '/DNOMINMAX', '/DNDEBUG',
             '/D_USE_MATH_DEFINES', '/D_HAS_STD_BYTE=0', '/DWINVER=0x0A00',
             '/D_WIN32_WINNT=0x0A00', '/DFOXICMP_PRIVATE_CLASSIC_SEEK_TEST=1',
             '/Iinclude', '/Isrc', '/Isrc/ExtLib/ffmpeg', '/Isrc/ExtLib/mfx_dispatch/include']
    command('splitter', ['cl.exe', *flags, '/c',
            'src/filters/parser/BaseSplitter/BaseSplitter.cpp', '/Fo' + str(output / 'BaseSplitter.obj')])
    text = read_rsp(args.link_rsp)
    # Redirect every explicit normal object and linker output from the coherent
    # input build folder. Preserve other libraries/resources outside that folder.
    for old in (str(objects), str(objects).replace('\\', '/')):
        text = re.sub(re.escape(old), lambda _: str(output), text, flags=re.IGNORECASE)
    for source in ('MainFrm', 'ClassicProgressiveApi'):
        assert re.search(r'[/\\]' + source + r'\.obj"', text, flags=re.IGNORECASE), source
    text = '"' + str(output / 'BaseSplitter.obj') + '" ' + text
    rsp = output / 'link.rsp'
    rsp.write_text(text, encoding='utf-16')
    command('link', ['link.exe', '@' + str(rsp)])
    stage = output / 'player'
    shutil.copytree(normal_player.parent, stage)
    shutil.copyfile(output / normal_player.name, stage / normal_player.name)
    (stage / normal_player.name).with_suffix('.ini').write_text(
        '[Settings]\nContextMenuPrompted=1\nContextMenuEnabled=0\n'
        'CheckForUpdates=0\nKeepHistory=0\n', encoding='utf-16')
    (output / 'build.json').write_text(json.dumps(dict(result='PASS', nativeExits=receipts,
        privateMacro=True, shippingProjectChanged=False, player=str(stage / normal_player.name)), indent=2) + '\n')
    print(json.dumps(dict(result='PASS', player=str(stage / normal_player.name))))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--normal-objects', type=Path, required=True)
    parser.add_argument('--normal-player', type=Path, required=True)
    parser.add_argument('--main-rsp', type=Path, required=True)
    parser.add_argument('--api-rsp', type=Path, required=True)
    parser.add_argument('--link-rsp', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    build(parser.parse_args())
