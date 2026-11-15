"""Framing controls for the test-only audio-count study, not decoder tests."""
import unittest
from progressive_audio_count_prototype import AudioCountPrototype, packet_samples
from progressive_simple_vfr_prototype import Invalid


class OpusFraming(unittest.TestCase):
    def test_twenty_ms_and_dtx(self):
        self.assertEqual(packet_samples(b'\xf8\x01'), 960)
        self.assertEqual(packet_samples(b'\xf8'), 960)  # Zero-byte DTX frame.

    def test_multiple_frames_are_counted_but_not_enabled_by_the_study(self):
        self.assertEqual(packet_samples(b'\xf9\x01\x02'), 1920)
        self.assertEqual(packet_samples(b'\xfa\x01\x01\x02\x03'), 1920)
        self.assertEqual(packet_samples(b'\xfb\x01\x01'), 960)

    def test_truncated_frame_count(self):
        with self.assertRaisesRegex(Invalid, 'truncated-opus-count'):packet_samples(b'\xfb')

    def test_zero_frame_count(self):
        with self.assertRaisesRegex(Invalid, 'invalid-opus-count'):packet_samples(b'\xfb\0')

    def test_duration_over_120_ms(self):
        with self.assertRaisesRegex(Invalid, 'opus-duration-limit'):packet_samples(b'\xfb\x07')

    def test_missing_padding_length(self):
        with self.assertRaisesRegex(Invalid, 'truncated-opus-padding'):packet_samples(b'\xfb\x41')

    def test_padding_overrun(self):
        with self.assertRaisesRegex(Invalid, 'opus-padding-overrun'):packet_samples(b'\xfb\x41\xff')

    def test_truncated_vbr_length(self):
        with self.assertRaisesRegex(Invalid, 'truncated-opus-frame-length'):packet_samples(b'\xfa\xfc')

    def test_vbr_frame_overrun(self):
        with self.assertRaisesRegex(Invalid, 'opus-frame-overrun'):packet_samples(b'\xfa\x05\x01')

    def test_cbr_unequal_frames(self):
        with self.assertRaisesRegex(Invalid, 'opus-cbr-framing'):packet_samples(b'\xf9\x01')

    def test_oversize_cbr_frame(self):
        with self.assertRaisesRegex(Invalid, 'opus-cbr-framing'):packet_samples(b'\xf8'+b'\x01'*1276)

    def test_empty_packet(self):
        with self.assertRaisesRegex(Invalid, 'empty-opus-packet'):packet_samples(b'')
        p = AudioCountPrototype(100, 1, 400000)
        for malformed in (b'', b'\x82', b'\x82\0\0\0'):
            with self.assertRaisesRegex(Invalid, 'empty-block'):
                p._block(0xa3, malformed, 0, malformed)


if __name__ == '__main__':unittest.main()
