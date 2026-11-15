"""Real encoded controls for boundaries deliberately excluded from the proposal."""
from pathlib import Path
import tempfile
import unittest

from progressive_audio_count_preflight import conversion, rewrite_audio
from progressive_simple_vfr_preflight import head_without_default
from progressive_simple_vfr_prototype import Invalid
from progressive_webm_opus_remux_preflight import fixture


class AudioCountLimits(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='fox-audio-count-limits-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.raw = {}
        for name, ms, samples in [('one-packet', '20', 1), ('ten-ms', '10', 9600)]:
            source, _ = fixture(Path(cls.temporary.name)/name,
                (name, False, 1, ms, 'audio', samples, True))
            cls.raw[name] = head_without_default(source.read_bytes())

    def test_one_packet_is_not_silently_enabled(self):
        with self.assertRaisesRegex(Invalid, 'audio-final-count'):
            conversion(self.raw['one-packet'], 600, 240000000, 8191)

    def test_other_opus_duration_is_not_silently_enabled(self):
        with self.assertRaisesRegex(Invalid, 'unsupported-opus-packet-duration'):
            conversion(self.raw['ten-ms'], 600, 240000000, 8191)

    def test_absent_audio_payload_is_not_a_complete_required_track(self):
        raw = rewrite_audio(self.raw['one-packet'], lambda *_: b'')
        with self.assertRaisesRegex(Invalid, 'audio-final-count'):
            conversion(raw, 600, 240000000, 8191)


if __name__ == '__main__':unittest.main()
