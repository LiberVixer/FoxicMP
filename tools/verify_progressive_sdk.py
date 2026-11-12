#!/usr/bin/env python3
"""Verify an SDK ZIP's manifest, safe file layout and standalone Python imports."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import stat
import subprocess
import sys
import tempfile
import zipfile


def verify(path, require_clean=False):
    with zipfile.ZipFile(path) as archive:
        names=archive.namelist()
        if len(names)!=len(set(names)) or sum(i.file_size for i in archive.infolist())>256*1024*1024:
            raise ValueError('Duplicate entries or oversized SDK')
        for info in archive.infolist():
            p=PurePosixPath(info.filename)
            if p.is_absolute() or '..' in p.parts or '\\' in info.filename or ':' in info.filename or stat.S_ISLNK(info.external_attr>>16):
                raise ValueError('Unsafe SDK entry')
        manifest=json.loads(archive.read('sdk-manifest.json'))
        if manifest.get('sdkVersion') not in ('1.82','1.83','1.84','1.85','1.86','1.87','1.88') or manifest.get('continuousRemux',{}).get('mode')!='continuous-h264-video-v1':
            raise ValueError('Unexpected SDK contract/version')
        if manifest['sdkVersion'] in ('1.83','1.84','1.85','1.86','1.87','1.88'):
            contract=manifest['continuousRemux'];mode='continuous-h264-reordered-video-v1'
            profile=contract.get('profiles',{}).get(mode,{})
            if (contract.get('modeSelection')!='continuousRemuxStart.mode' or profile.get('mode')!=mode
                    or profile.get('bFrames') is not True or profile.get('audio') is not False
                    or profile.get('inheritsCommonContract') is not True or profile.get('explicitSelectionRequired') is not True):
                raise ValueError('Missing explicit reordered profile')
        if manifest['sdkVersion'] in ('1.84','1.85','1.86','1.87','1.88'):
            mode='continuous-h264-aac-window-v1';profile=manifest['continuousRemux'].get('profiles',{}).get(mode,{})
            expected=dict(version=1,mode=mode,containerHint='mp4',videoCodec='h264',videoOnly=False,audio=True,
                audioCodec='aac-lc',bFrames=True,sampleRates=[44100,48000],channels=[1,2],requiredTrackKinds=['video','audio'],
                sharedResourceRequired=True,pcmWindowRequired=True,pcmWindowVersion=1,initialSkipSamples=1024,
                presentationSamplesRequired=True,inheritsCommonContract=True,explicitSelectionRequired=True)
            # JSON equality alone considers true == 1; retain exact scalar types.
            if json.dumps(profile,sort_keys=True)!=json.dumps(expected,sort_keys=True):
                raise ValueError('Missing exact explicit AAC PCM-window profile')
        if manifest['sdkVersion'] in ('1.85','1.86','1.87','1.88'):
            expected=dict(version=1,field='continuousRemuxPlay.startSeconds',range='published-presentation-prefix',completeRange='source-duration',pending='caller-retry',decoderMayBuffer=True)
            if json.dumps(manifest['continuousRemux'].get('initialPosition'),sort_keys=True)!=json.dumps(expected,sort_keys=True):
                raise ValueError('Missing explicit initial-position contract')
        if manifest['sdkVersion'] in ('1.86','1.87','1.88'):
            expected=dict(version=1,field='continuousPlaybackControl.allowGrowing',range='published-presentation-prefix',unavailable='waiting-range',maxSeeksPerPlayback=16,decoderMayBuffer=True)
            if json.dumps(manifest['continuousRemux'].get('growingSeek'),sort_keys=True)!=json.dumps(expected,sort_keys=True):
                raise ValueError('Missing bounded growing-seek contract')
        if manifest['sdkVersion'] in ('1.87','1.88'):
            expected=dict(version=1,field='continuousPlaybackControl.waitForRange',requiresGrowingOptIn=True,maxPending=1,
                superseding='latest-target',cancelCommand='cancelSeek',previousPlaybackUntilCommit=True)
            if json.dumps(manifest['continuousRemux'].get('queuedSeek'),sort_keys=True)!=json.dumps(expected,sort_keys=True):
                raise ValueError('Missing explicit queued-seek contract')
        if manifest['sdkVersion']=='1.88':
            mode='continuous-hevc-reordered-video-v1'
            expected=dict(version=1,mode=mode,containerHint='mp4',videoCodec='hevc',videoOnly=True,audio=False,
                bFrames=True,inheritsCommonContract=True,explicitSelectionRequired=True,
                hevcProfiles=['main','main10'],bitDepths=[8,10],chromaSubsampling='4:2:0',
                sampleEntries=['hvc1','hev1'],nalLengthBytes=4,singleLayer=True,
                requiredTrackKinds=['video'],codecHintRequired=True,maxParameterSetsPerType=1,firstPicture='irap')
            profile=manifest['continuousRemux'].get('profiles',{}).get(mode,{})
            if json.dumps(profile,sort_keys=True)!=json.dumps(expected,sort_keys=True):
                raise ValueError('Missing exact explicit HEVC video profile')
        if require_clean and manifest.get('sourceDirty') is not False:
            raise ValueError('SDK was packaged from an uncommitted tree')
        files=manifest['files']
        if set(names)!=set(files)|{'sdk-manifest.json'}:
            raise ValueError('SDK file list differs from manifest')
        for name,digest in files.items():
            if hashlib.sha256(archive.read(name)).hexdigest()!=digest:
                raise ValueError('SDK hash mismatch: '+name)
        modules=sorted(name for name in files if name.endswith('.py') and name.startswith('progressive_'))
        if len(modules)!=10:
            raise ValueError('Expected all ten SDK modules')
        with tempfile.TemporaryDirectory(prefix='foxicmp-sdk-verify-') as directory:
            for name in modules:
                if '/' in name:raise ValueError('SDK modules must be flat')
                Path(directory,name).write_bytes(archive.read(name))
            imports=[Path(name).stem for name in modules if name!='progressive_windows.py' or sys.platform=='win32']
            code='import sys;sys.path.insert(0,sys.argv[1]);'+''.join('import '+name+';' for name in imports)
            result=subprocess.run([sys.executable,'-B','-I','-c',code,directory],capture_output=True,text=True,timeout=10)
            if result.returncode:raise RuntimeError(result.stderr)
    return dict(result='PASS',sdkVersion=manifest['sdkVersion'],sourceCommit=manifest['sourceCommit'],
        sourceDirty=manifest['sourceDirty'],filesVerified=len(files),modulesVerified=len(modules),
        standaloneImportExitCode=result.returncode,sha256=hashlib.sha256(Path(path).read_bytes()).hexdigest())


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('sdk',type=Path);p.add_argument('--require-clean',action='store_true');args=p.parse_args()
    print(json.dumps(verify(args.sdk,args.require_clean)))
