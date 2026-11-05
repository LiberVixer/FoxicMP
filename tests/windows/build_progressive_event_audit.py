"""Relink an already built Classic into a new, private diagnostic directory.

Run from an x64 Visual Studio developer prompt. Never changes the vcxproj or
normal link outputs. --prepare-only writes commands/response files for review.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--link-log', type=Path)
    parser.add_argument('--detours-library', type=Path)
    parser.add_argument('--prepare-only', action='store_true')
    args = parser.parse_args()
    assert os.name == 'nt', 'Use the Windows x64 Visual Studio developer prompt'
    root = Path(__file__).resolve().parents[2]
    log = args.link_log or root/'_bin/obj/Release_x64/FoxicMP/FoxicMP.tlog/link.command.1.tlog'
    library = args.detours_library or root/'_bin/lib/Release_x64/Detours.lib'
    directory = args.directory.resolve()
    # Reject existing folders, including the normal output directory.
    directory.mkdir(parents=True, exist_ok=False)
    text = '\n'.join(line for line in log.read_text(encoding='utf-16').splitlines()
                     if line and not line.startswith('^'))
    assert text and library.is_file()
    originals = set()
    output_flags = ('OUT','PDB','IMPLIB','LTCGOUT','ILK','MAP','PGD','PDBSTRIPPED','MANIFESTFILE')
    counts = {}
    for flag in output_flags:
        def replace(match):
            original = Path(match.group(1)).resolve()
            originals.add(original)
            if flag == 'LTCGOUT':
                originals.add(original.with_suffix('.ipdb'))
            return '/'+flag+':"'+str(directory/original.name)+'"'
        text, counts[flag] = re.subn('/'+flag+r':"([^"]+)"', replace, text, flags=re.I)
    assert all(counts[flag] == 1 for flag in ('OUT','PDB','IMPLIB','LTCGOUT')), counts
    before = {str(path):digest(path) for path in originals if path.is_file()}
    audit_object = directory/'event-audit.obj'
    text += '\n"'+str(audit_object)+'"\n'
    response = directory/'classic-link.rsp'
    response.write_text(text, encoding='utf-16')
    common = ['/nologo','/O2','/EHsc','/MT','/std:c++17','/DUNICODE','/D_UNICODE']
    commands = [
        ['cl.exe',*common,'/c','/Fo'+str(audit_object),'/Fd'+str(directory/'audit.pdb'),
         str(root/'tests/windows/progressive_event_audit.cpp')],
        ['cl.exe',*common,'/Fo'+str(directory/'fixture.obj'),'/Fd'+str(directory/'fixture.pdb'),
         '/Fe'+str(directory/'event-audit-test.exe'),str(root/'tests/windows/progressive_event_audit_test.cpp'),
         str(audit_object),str(library),'user32.lib','/link','/INCREMENTAL:NO'],
        ['link.exe','@'+str(response)],
    ]
    manifest = dict(prepareOnly=args.prepare_only,originalOutputsBefore=before,
        linkLogSha256=digest(log),sourceSha256={p.name:digest(p) for p in
        sorted((root/'tests/windows').glob('progressive_event_audit*'))},commands=commands,exits=[])
    destination = directory/'build.json'
    destination.write_text(json.dumps(manifest, indent=2)+'\n')
    code = 0
    try:
        if not args.prepare_only:
            for command in commands:
                code = subprocess.run(command, cwd=root).returncode
                manifest['exits'].append(code)
                if code:
                    break
    finally:
        after = {path:digest(Path(path)) if Path(path).is_file() else None for path in before}
        manifest['originalOutputsUnchanged'] = before == after
        manifest['actualExitCode'] = code
        destination.write_text(json.dumps(manifest, indent=2)+'\n')
        assert before == after, 'Normal link outputs changed during the diagnostic build'
    return code


if __name__ == '__main__':
    sys.exit(main())
