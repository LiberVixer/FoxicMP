"""Validate a private QtCore rebuild against the installed 6.12.0 UCRT package."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import struct


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def validate(prefix, work):
    here=Path(__file__).resolve().parent
    pin=json.loads((here/'qt-6.12.0-long-lock-paths.json').read_text())
    prefix,work=prefix.resolve(),work.resolve()
    build=work/'build'
    source=work/('source/qtbase-everywhere-src-'+pin['version'])
    fixed=build/'bin/Qt6Core.dll'
    stock=prefix/'bin/Qt6Core.dll'
    export_tool=here.parents[1]/'qt/packaging/windows/build_winpthread.py'
    spec=importlib.util.spec_from_file_location('foxicmp_pe_exports',export_tool)
    exports=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(exports)
    def machine(path):
        raw=path.read_bytes();pe=struct.unpack_from('<I',raw,0x3c)[0]
        return struct.unpack_from('<H',raw,pe+4)[0]
    if machine(stock)!=0x8664 or machine(fixed)!=0x8664:
        raise ValueError('Expected matching x86_64 QtCore libraries')
    old,new=exports.export_names(stock),exports.export_names(fixed)
    if old-new:
        raise ValueError('QtCore exports missing: '+str(sorted(old-new)))
    headers=[('qtcore-config.h','qtcore-config.h'),
             ('6.12.0/QtCore/private/qtcore-config_p.h','qtcore-config_p.h'),
             ('qconfig.h','global/qconfig.h'),
             ('6.12.0/QtCore/private/qconfig_p.h','global/qconfig_p.h')]
    comparisons=[]
    for left,right in headers:
        a=prefix/'include/qt6/QtCore'/left
        b=build/'src/corelib'/right
        before=dict(re.findall(r'^#define (QT_FEATURE_\w+) (-?\d+)',a.read_text(),re.M))
        after=dict(re.findall(r'^#define (QT_FEATURE_\w+) (-?\d+)',b.read_text(),re.M))
        if not before or before!=after:
            raise ValueError('QtCore feature mismatch: '+left+' '+str({k:(before.get(k),after.get(k)) for k in before.keys()|after.keys() if before.get(k)!=after.get(k)}))
        comparisons.append(dict(stock=left,built=right,featureCount=len(before),equal=True,
                                stockSha256=sha(a),builtSha256=sha(b)))
    if sha(source/'src/corelib/io/qlockfile_win.cpp')!=pin['qlockfileAfterSha256']:
        raise ValueError('QtCore source does not match reviewed patch')
    if sha(source/'src/corelib/io/qfilesystemengine_win.cpp')!=pin['tempPathAfterSha256']:
        raise ValueError('QtCore temporary-path source does not match reviewed patch')
    notices=work/'qtcore-licenses'
    shutil.copytree(source/'LICENSES',notices/'qt',dirs_exist_ok=True)
    cpp_license=work/'deps/ucrt64/share/licenses/cppwinrt'
    if not cpp_license.is_dir():
        raise ValueError('Missing private CppWinRT license notices')
    shutil.copytree(cpp_license,notices/'cppwinrt',dirs_exist_ok=True)
    result=dict(architecture='x86_64',qtVersion=pin['version'],upstreamCommit=pin['commit'],archiveSha256=pin['archiveSha256'],
        localPatchSha256=pin['localPatchSha256'],sourceSha256=pin['qlockfileAfterSha256'],
        stockLibrarySha256=sha(stock),librarySha256=sha(fixed),stockExports=len(old),fixedExports=len(new),
        allStockExportsPreserved=True,allFeatureFlagsEqual=True,featureHeaders=comparisons,
        sourcePin=pin,toolchainModified=False,fix='Native lock deletion/stale reads and bounded dynamic Windows temporary-path buffers; retain atomic sync and interprocess locking')
    shutil.copy2(fixed,work/'Qt6Core.dll')
    (work/'qtcore-longpaths-build.json').write_text(json.dumps(result,indent=2)+'\n')
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--toolchain-prefix',type=Path,required=True)
    parser.add_argument('--work',type=Path,required=True)
    args=parser.parse_args()
    print(json.dumps(validate(args.toolchain_prefix,args.work)))
