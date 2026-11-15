"""Boundaries of the variable-duration study, not product admission tests."""
import unittest

from progressive_audio_count_prototype import packet_samples
from progressive_opus_geometry_prototype import OpusGeometryPrototype
from progressive_simple_vfr_prototype import Cluster, Invalid
from progressive_webm_vfr_preflight import ebml


class Geometry(unittest.TestCase):
    def clock(self, skip=312, **limits):
        p = OpusGeometryPrototype(10000, 2, 800000, **limits)
        p.video, p.audio, p.pre_skip = 1, 2, skip
        p.current = Cluster(0, None, clock=0);p.queue.append(p.current)
        return p

    def add(self, p, packet, *, ticks=None, trim=None):
        if ticks is None:ticks = (p.decoded_samples+24)//48
        block = b'\x82'+ticks.to_bytes(2, 'big', signed=True)+b'\x80'+packet
        raw = ebml(0xa3, block);tag = 0xa3;header = len(raw)-len(block)
        if trim is not None:
            body = ebml(0xa1, block)+ebml(0x75a2, trim.to_bytes(8, 'big', signed=True))
            raw = ebml(0xa0, body);tag = 0xa0;header = len(raw)-len(body)
        p._block(tag, raw, header, block)

    def test_short_first_packets_do_not_underflow_pre_skip(self):
        for skip in (120, 312):
            p = self.clock(skip)
            for _ in range(4):self.add(p, b'\x80')  # 2.5ms, valid empty DTX frame.
            self.assertEqual(p.decoded_samples, 480)
            self.assertEqual(p.indexed_audio_samples, max(0, 360-skip))

    def test_variable_duration_is_summed_instead_of_packet_count(self):
        p = self.clock()
        for packet in (b'\x80', b'\x88', b'\x90', b'\x98', b'\x99\0\0', b'\x9b\x06'):
            self.add(p, packet)
        self.assertEqual(p.decoded_samples, 120+240+480+960+1920+5760)
        self.assertEqual(p.indexed_audio_samples, 120+240+480+960+1920-312)
        self.assertFalse(p.audio_sample_count_final)

    def test_all_toc_configs(self):
        for config in range(32):
            with self.subTest(config=config):
                n = packet_samples(bytes([config << 3]))
                self.assertGreaterEqual(n, 120);self.assertLessEqual(n, 2880)
                p = self.clock();self.add(p, bytes([config << 3]))
                self.assertEqual(p.decoded_samples, n)

    def test_forty_eight_short_frames(self):
        p = self.clock();self.add(p, b'\x83\x30')
        self.assertEqual(p.decoded_samples, 5760)

    def test_more_than_120ms(self):
        with self.assertRaisesRegex(Invalid, 'opus-duration-limit'):
            self.add(self.clock(), b'\x9b\x07')

    def test_terminal_trim_uses_current_packet_duration(self):
        for packet in (b'\x80', b'\x88', b'\x90', b'\x98', b'\x9b\x06'):
            n = packet_samples(packet);p = self.clock()
            self.add(p, packet, trim=((n-1)*1000000000)//48000)
            self.assertEqual(p.end_trim_samples, n-1)
            self.assertTrue(p.padding_seen);self.assertFalse(p.audio_sample_count_final)
            with self.assertRaisesRegex(Invalid, 'audio-after-end-trim'):self.add(p, packet)

    def test_whole_packet_trim_is_outside_profile(self):
        for packet in (b'\x80', b'\x98', b'\x9b\x06'):
            n = packet_samples(packet)
            with self.assertRaisesRegex(Invalid, 'opus-end-trim'):
                self.add(self.clock(), packet, trim=n*1000000000//48000)

    def test_fractional_sample_trim(self):
        with self.assertRaisesRegex(Invalid, 'opus-end-trim'):
            self.add(self.clock(), b'\x98', trim=1)

    def test_negative_trim(self):
        with self.assertRaisesRegex(Invalid, 'opus-end-trim'):
            self.add(self.clock(), b'\x98', trim=-20833)

    def test_bad_clock(self):
        p = self.clock();self.add(p, b'\x9b\x06')
        with self.assertRaisesRegex(Invalid, 'opus-clock'):self.add(p, b'\x80', ticks=0)

    def test_zero_trim_does_not_end_track(self):
        p = self.clock();self.add(p, b'\x98', trim=0);self.add(p, b'\x80')
        self.assertFalse(p.padding_seen);self.assertFalse(p.audio_sample_count_final)

    def test_sample_budget(self):
        p = self.clock(sample_limit=1000);self.add(p, b'\x9b\x06')
        with self.assertRaisesRegex(Invalid, 'audio-budget'):self.add(p, b'\x80')

    def test_packet_budget(self):
        p = self.clock(packet_limit=2);self.add(p, b'\x80');self.add(p, b'\x80')
        with self.assertRaisesRegex(Invalid, 'audio-budget'):self.add(p, b'\x80')

    def test_duplicate_trim(self):
        p = self.clock();block = b'\x82\0\0\x80\x98'
        body = ebml(0xa1, block)+ebml(0x75a2, b'\0')*2;raw = ebml(0xa0, body)
        with self.assertRaisesRegex(Invalid, 'opus-block-group'):
            p._block(0xa0, raw, len(raw)-len(body), block)

    def test_lacing_not_enabled(self):
        p = self.clock();block = b'\x82\0\0\x82\x98';raw = ebml(0xa3, block)
        with self.assertRaisesRegex(Invalid, 'opus-block-flags'):
            p._block(0xa3, raw, len(raw)-len(block), block)


if __name__ == '__main__':unittest.main()
