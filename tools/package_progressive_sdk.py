#!/usr/bin/env python3
"""Build a reproducible player-only integration SDK ZIP, without media/binaries."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
FILES = {
    **{name: 'qt/tools/' + name for name in (
        'progressive_client.py', 'progressive_windows.py', 'progressive_sdk.py', 'progressive_writer.py',
        'progressive_schema.py')},
    'progressive-v2-vod.json': 'tests/fixtures/progressive-v2/vod.json',
    'README.ru.md': 'docs/ProgressiveSdk.md',
    'ProgressiveSdk.md': 'docs/ProgressiveSdk.md',
    'LinuxProgressiveApi.md': 'docs/LinuxProgressiveApi.md',
    'LICENSE.txt': 'LICENSE.txt',
    **{name: 'docs/' + name for name in (
        'ProgressiveIntegrationPlan.md', 'ProgressiveTechnicalSpec-2026-10-25.ru.md',
        'ProgressiveV1Validation-2026-10-25.md', 'ProgressiveV1Validation-2026-10-25.json',
        'ProgressiveSdkValidation-2026-10-25.md', 'ProgressiveSdkValidation-2026-10-25.json',
        'ProgressiveManifestV2.md', 'ProgressiveModelValidation-2026-10-25.md',
        'ProgressiveModelValidation-2026-10-25.json', 'ProgressiveReaderLeases.md',
        'ProgressiveReaderValidation-2026-10-25.md', 'ProgressiveReaderValidation-2026-10-25.json')},
}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    content = {name: (ROOT / source).read_bytes() for name, source in FILES.items()}
    version = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT))
    manifest = dict(schemaVersion=1, protocol='progressive-manifest-v1', sdkVersion='1.4', readerLeaseProtocol=1,
        playbackSchemaVersions=[1], validationSchemaVersions=[2],
        sourceCommit=version, sourceDirty=dirty,
        files={name: hashlib.sha256(data).hexdigest() for name, data in sorted(content.items())})
    content['sdk-manifest.json'] = (json.dumps(manifest, indent=2)+'\n').encode()
    with zipfile.ZipFile(args.output, 'x', zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(content.items()):
            info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, data)
    print(json.dumps(dict(path=str(args.output), sha256=hashlib.sha256(args.output.read_bytes()).hexdigest(),
                         sourceCommit=version, sourceDirty=dirty)))

if __name__ == '__main__':
    main()
