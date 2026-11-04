"""Linux package update preserves profiles and publishes the player last."""
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
launcher=None
if os.name!='nt':
    spec=importlib.util.spec_from_file_location('foxiqmp_package_launcher',ROOT/'qt/packaging/debian/launcher.py')
    launcher=importlib.util.module_from_spec(spec);spec.loader.exec_module(launcher)


@unittest.skipIf(os.name=='nt','Linux package updater; not a Windows acceptance test')
class Test(unittest.TestCase):
    def layout(self,root):
        source=root/'payload';source.mkdir();destination=root/'personal';destination.mkdir()
        (source/'.payload-id').write_text('sdk-1.82')
        (source/'foxicmp-qt').write_bytes(b'new player');(source/'foxicmp-qt').chmod(0o755)
        (source/'foxicmp-remux-stream-worker').write_bytes(b'new worker');(source/'foxicmp-remux-stream-worker').chmod(0o755)
        (source/'integration').mkdir();(source/'integration/progressive_continuous.py').write_text('# sdk\n')
        (destination/'foxicmp-qt').write_bytes(b'old player')
        (destination/'FoxicMPQt.ini').write_bytes(b'personal settings')
        (destination/'playlist.mpcpl').write_bytes(b'personal playlist')
        return source,destination

    def test_helpers_precede_player_without_touching_profile(self):
        with tempfile.TemporaryDirectory() as name:
            source,destination=self.layout(Path(name));order=[];replace=launcher.os.replace
            def record(src,dst):order.append(Path(dst).relative_to(destination).as_posix());return replace(src,dst)
            with patch.object(launcher.os,'replace',side_effect=record):launcher.stage_payload(source,destination)
            self.assertLess(order.index('foxicmp-remux-stream-worker'),order.index('foxicmp-qt'))
            self.assertLess(order.index('integration/progressive_continuous.py'),order.index('foxicmp-qt'))
            self.assertEqual(order[-1],'.payload-id')
            self.assertEqual((destination/'FoxicMPQt.ini').read_bytes(),b'personal settings')
            self.assertEqual((destination/'playlist.mpcpl').read_bytes(),b'personal playlist')
            self.assertEqual((destination/'foxicmp-remux-stream-worker').stat().st_mode & 0o777,0o755)

    def test_factory_payload_may_not_overwrite_personal_ini(self):
        with tempfile.TemporaryDirectory() as name:
            source,destination=self.layout(Path(name));(source/'FoxicMPQt.ini').write_text('forbidden profile')
            self.assertRaises(RuntimeError,launcher.stage_payload,source,destination)
            self.assertEqual((destination/'FoxicMPQt.ini').read_bytes(),b'personal settings')
            self.assertEqual((destination/'foxicmp-qt').read_bytes(),b'old player')
            self.assertFalse((destination/'.payload-id').exists())

    def test_same_payload_is_not_reinstalled(self):
        with tempfile.TemporaryDirectory() as name:
            source,destination=self.layout(Path(name));launcher.stage_payload(source,destination)
            with patch.object(launcher.os,'replace',side_effect=AssertionError('unexpected replacement')):
                launcher.stage_payload(source,destination)


if __name__=='__main__':unittest.main()
