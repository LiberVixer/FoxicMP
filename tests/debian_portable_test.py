import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('deb_launcher', ROOT / 'qt/packaging/debian/launcher.py')
launcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launcher)

class DebianPortableTest(unittest.TestCase):
    def test_update_preserves_profile_and_running_inode(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / 'system'; source.mkdir()
            target = Path(temp) / 'home/app'
            (source / '.payload-id').write_text('v1')
            (source / 'foxicmp-qt').write_bytes(b'old player')
            (source / 'foxicmp-qt').chmod(0o755)
            launcher.stage_payload(source, target)
            (target / 'FoxicMPQt.ini').write_bytes(b'personal profile')
            (target / 'History').mkdir(); (target / 'History/list').write_bytes(b'personal history')
            with (target / 'foxicmp-qt').open('rb') as running:
                (source / '.payload-id').write_text('v2')
                (source / 'foxicmp-qt').write_bytes(b'new player')
                launcher.stage_payload(source, target)
                self.assertEqual(running.read(), b'old player')
            self.assertEqual((target / 'foxicmp-qt').read_bytes(), b'new player')
            self.assertEqual((target / 'FoxicMPQt.ini').read_bytes(), b'personal profile')
            self.assertEqual((target / 'History/list').read_bytes(), b'personal history')
            self.assertEqual((target / 'foxicmp-qt').stat().st_mode & 0o777, 0o755)

    def test_failed_update_does_not_commit_marker_and_can_retry(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / 'system'; source.mkdir()
            target = Path(temp) / 'app'
            (source / '.payload-id').write_text('new')
            (source / 'foxicmp-qt').write_bytes(b'player')
            with patch.object(launcher.shutil, 'copyfileobj', side_effect=OSError('disk full')):
                with self.assertRaises(OSError): launcher.stage_payload(source, target)
            self.assertFalse((target / '.payload-id').exists())
            self.assertFalse([p for p in target.glob('.package-*') if p.name != '.package-update.lock'])
            launcher.stage_payload(source, target)
            self.assertEqual((target / '.payload-id').read_text(), 'new')

    def test_personal_profile_in_payload_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / 'system'; source.mkdir()
            (source / '.payload-id').write_text('unsafe')
            (source / 'FoxicMPQt.ini').write_text('private')
            target = Path(temp) / 'app'
            with self.assertRaises(RuntimeError): launcher.stage_payload(source, target)
            self.assertFalse((target / 'FoxicMPQt.ini').exists())

if __name__ == '__main__': unittest.main()
