#!/usr/bin/env python3
"""Generate independent per-user WiX 3 MSI/Burn projects from clean release stages."""
import argparse
import hashlib
from pathlib import Path
import uuid
import xml.etree.ElementTree as E

NS = 'http://schemas.microsoft.com/wix/2006/wi'
BAL = 'http://schemas.microsoft.com/wix/BalExtension'
E.register_namespace('', NS)
E.register_namespace('bal', BAL)
NAMESPACE = uuid.UUID('8a741f14-101b-4523-a3d1-67df2b661c07')
EDITIONS = {
    'classic': ('FoxicMP Classic', 'FoxicMPClassic', 'FoxicMP64.exe', 'FoxicMP64.ini', 'src/apps/mplayerc/res/icon.ico'),
    'qt': ('FoxiqMP qt', 'FoxiqMPqt', 'FoxicMPq.exe', 'FoxicMPQt.ini', 'qt/resources/foxicmp.ico'),
}

def guid(name):
    return str(uuid.uuid5(NAMESPACE, name)).upper()

def ident(prefix, name):
    return prefix + hashlib.sha256(name.encode()).hexdigest()[:28]

def child(parent, name, **attrs):
    return E.SubElement(parent, '{' + NS + '}' + name, {k: str(v) for k, v in attrs.items()})

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--edition', choices=EDITIONS, required=True)
    p.add_argument('--version', default='0.2.0')
    p.add_argument('--stage', type=Path, required=True)
    p.add_argument('--windows-stage', required=True)
    p.add_argument('--windows-repo', required=True)
    p.add_argument('--windows-output', required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    title, folder, exe, ini, icon = EDITIONS[args.edition]
    regkey = 'Software\\FoxicMP\\Installers\\' + args.edition
    stage = args.stage.resolve()
    assert (stage / exe).is_file(), 'Missing player'
    if args.edition == 'classic':
        defaults = Path(__file__).resolve().parents[2] / 'distrib/Defaults/FoxicMP.ini'
        assert (stage / ini).read_bytes() == defaults.read_bytes(), 'Classic stage must contain factory defaults, not a personal INI'
    files = sorted(f for f in stage.rglob('*') if f.is_file())
    for f in files:
        rel = f.relative_to(stage)
        if 'licenses' not in rel.parts:
            assert f.suffix.lower() not in ('.pdb', '.lib', '.exp'), f
        if args.edition == 'qt':
            assert f.name.casefold() != ini.casefold(), 'Never package personal Qt profiles'
    args.output.mkdir(parents=True, exist_ok=True)
    base = ('FoxicMPClassic' if args.edition == 'classic' else 'FoxicMPQt') + f'-{args.version}-windows-x64'
    source = args.windows_stage.rstrip('/\\')
    repo = args.windows_repo.rstrip('/\\')
    root = E.Element('{' + NS + '}Wix')
    product = child(root, 'Product', Id=guid(args.edition + '/product/' + args.version), Name=title,
                    Language='1033', Version=args.version, Manufacturer='FoxicMP Team',
                    UpgradeCode=guid(args.edition + '/msi-upgrade'))
    child(product, 'Package', InstallerVersion='500', Compressed='yes', InstallScope='perUser', InstallPrivileges='limited', Platform='x64', Description=title + ' ' + args.version)
    child(product, 'MajorUpgrade', DowngradeErrorMessage='A newer version is already installed.', Schedule='afterInstallInitialize')
    child(product, 'MediaTemplate', EmbedCab='yes', CompressionLevel='high')
    child(product, 'Property', Id='ARPPRODUCTICON', Value='ProductIcon')
    child(product, 'Icon', Id='ProductIcon', SourceFile=repo + '/' + icon)
    child(product, 'Property', Id='ARPURLINFOABOUT', Value='https://github.com/LiberVixer/FoxicMP')
    child(product, 'Property', Id='WIXUI_INSTALLDIR', Value='INSTALLDIR')
    previous = child(product, 'Property', Id='PREVIOUSINSTALLDIR')
    child(previous, 'RegistrySearch', Id='FindPreviousInstallDirectory', Root='HKCU',
          Key=regkey, Name='InstallDirectory', Type='raw', Win64='yes')
    # Registry-keypath components cannot recover their directory from a file
    # keypath during repair. Restore the selected location before costing.
    restore = child(product, 'SetProperty', Id='INSTALLDIR', Value='[PREVIOUSINSTALLDIR]',
                    Before='CostFinalize', Sequence='both')
    restore.text = 'PREVIOUSINSTALLDIR AND NOT INSTALLDIR'
    child(product, 'UIRef', Id='WixUI_InstallDir')
    child(product, 'WixVariable', Id='WixUILicenseRtf', Value=args.windows_output.rstrip('/\\') + '/license.rtf')
    dirs = child(product, 'Directory', Id='TARGETDIR', Name='SourceDir')
    local = child(dirs, 'Directory', Id='LocalAppDataFolder')
    programs = child(local, 'Directory', Id='UserPrograms', Name='Programs')
    install = child(programs, 'Directory', Id='INSTALLDIR', Name=folder)
    directory_map = {'': install}
    for rel in sorted({str(f.relative_to(stage).parent).replace('\\', '/') for f in files}):
        current = ''
        for part in ([] if rel == '.' else rel.split('/')):
            parent = current
            current = current + '/' + part if current else part
            if current not in directory_map:
                directory_map[current] = child(directory_map[parent], 'Directory', Id=ident('D', current), Name=part)
    components = []
    for f in files:
        rel = f.relative_to(stage).as_posix()
        parent = f.relative_to(stage).parent.as_posix()
        parent = '' if parent == '.' else parent
        cid = ident('C', rel)
        attrs = dict(Id=cid, Guid=guid(args.edition + '/file/' + rel), Win64='yes')
        # This one component deliberately uses the existing INI itself as its
        # key path: NeverOverwrite must protect profiles predating the MSI.
        profile = args.edition == 'classic' and rel == ini
        if profile:
            attrs.update(Permanent='yes', NeverOverwrite='yes')
        comp = child(directory_map[parent], 'Component', **attrs)
        child(comp, 'File', Id=ident('F', rel), Source=source + '/' + rel,
              KeyPath='yes' if profile else 'no')
        if not profile:
            child(comp, 'RegistryValue', Root='HKCU', Key=regkey + '\\Files',
                  Name=cid, Value='[INSTALLDIR]' + rel.replace('/', '\\'), Type='string', KeyPath='yes')
        components.append(cid)
    menu = child(dirs, 'Directory', Id='ProgramMenuFolder')
    menu = child(menu, 'Directory', Id='AppMenu', Name=title)
    comp = child(menu, 'Component', Id='MenuShortcut', Guid=guid(args.edition + '/menu'), Win64='yes')
    child(comp, 'Shortcut', Id='StartMenuShortcut', Name=title, Target='[INSTALLDIR]' + exe, WorkingDirectory='INSTALLDIR', Icon='ProductIcon')
    child(comp, 'RemoveFolder', Id='RemoveAppMenu', On='uninstall')
    child(comp, 'RegistryValue', Root='HKCU', Key=regkey, Name='InstallDirectory', Value='[INSTALLDIR]', Type='string', KeyPath='yes')
    components.append('MenuShortcut')
    cleanup = child(install, 'Component', Id='EmptyDirectoryCleanup',
                    Guid=guid(args.edition + '/empty-directories'), Win64='yes')
    for directory in [programs, *directory_map.values()]:
        child(cleanup, 'RemoveFolder', Id=ident('R', directory.attrib['Id']),
              Directory=directory.attrib['Id'], On='uninstall')
    child(cleanup, 'RegistryValue', Root='HKCU', Key=regkey, Name='DirectoryCleanup',
          Value='1', Type='integer', KeyPath='yes')
    components.append('EmptyDirectoryCleanup')
    feature = child(product, 'Feature', Id='Complete', Title=title, Level='1')
    for cid in components:
        child(feature, 'ComponentRef', Id=cid)
    E.ElementTree(root).write(args.output / (base + '.wxs'), encoding='utf-8', xml_declaration=True)
    bundle = E.Element('{' + NS + '}Wix')
    b = child(bundle, 'Bundle', Name=title + ' ' + args.version, Version=args.version,
              Manufacturer='FoxicMP Team', UpgradeCode=guid(args.edition + '/bundle-upgrade'),
              IconSourceFile=repo + '/' + icon)
    ba = child(b, 'BootstrapperApplicationRef', Id='WixStandardBootstrapperApplication.HyperlinkLicense')
    E.SubElement(ba, '{' + BAL + '}WixStandardBootstrapperApplication', LicenseUrl='https://github.com/LiberVixer/FoxicMP/blob/main/LICENSE.txt', SuppressOptionsUI='yes')
    variable = child(b, 'Variable', Name='InstallFolder', Type='string',
                     Value='[LocalAppDataFolder]Programs\\' + folder, Persisted='yes')
    variable.set('{' + BAL + '}Overridable', 'yes')
    chain = child(b, 'Chain')
    msi = child(chain, 'MsiPackage', SourceFile=args.windows_output.rstrip('/\\') + '/' + base + '.msi',
                DisplayInternalUI='yes', Vital='yes', Visible='no')
    child(msi, 'MsiProperty', Name='INSTALLDIR', Value='[InstallFolder]')
    E.ElementTree(bundle).write(args.output / (base + '-bundle.wxs'), encoding='utf-8', xml_declaration=True)
    # The full license also ships as LICENSE.txt in every application directory.
    (args.output / 'license.rtf').write_text(r'{\rtf1\ansi\deff0 {\fonttbl {\f0 Segoe UI;}}\f0\fs20 FoxicMP is free software under GNU GPL version 3.\par The complete license is included as LICENSE.txt in the installation folder and at https://www.gnu.org/licenses/gpl-3.0.html.\par Copyright 2002-2026 all contributors.}', encoding='ascii')
    print(base)

if __name__ == '__main__':
    main()
