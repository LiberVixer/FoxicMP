"""TEST ONLY: variable-duration Opus clock over confirmed WebM bytes.

No player, worker or SDK imports this study. Known video frames/end and source
length remain required by SimpleVfrPrototype; it is not a new playback mode.
"""
import struct

from progressive_audio_count_prototype import packet_samples
from progressive_simple_vfr_prototype import SimpleVfrPrototype, check
from progressive_webm_fixtures import width
from progressive_webm_opus_remux_preflight import children


class OpusGeometryPrototype(SimpleVfrPrototype):
    def __init__(self, length, frames, end100ns, *, sample_limit=48000000,
                 packet_limit=50000):
        super().__init__(length, frames, end100ns)
        check(649 <= sample_limit <= 48000000, 'audio-sample-limit')
        check(2 <= packet_limit <= 50000, 'audio-packet-limit')
        self.sample_limit, self.packet_limit = sample_limit, packet_limit
        self.audio_packets = self.decoded_samples = self.indexed_audio_samples = 0
        self.pre_skip = self.channels = self.last_duration = 0
        self.end_trim_samples = 0
        self.audio_samples = None
        self.audio_sample_count_final = self.padding_seen = False
        self.durations = set()

    @staticmethod
    def fields(body):
        fields = {}
        for key, _, a, z in children(body, 0, len(body)):
            check(key not in fields, 'duplicate-track-field')
            fields[key] = body[a:z]
        return fields

    def _configuration(self, body):
        super()._configuration(body)
        for tag, _, a, z in children(body, 0, len(body)):
            if tag != 0xae:continue
            fields = self.fields(body[a:z])
            if int.from_bytes(fields[0x83], 'big') != 2:continue
            private = fields.get(0x63a2, b'')
            check(len(private) == 19 and private[:9] == b'OpusHead\x01' and
                  private[9] in (1, 2) and private[16:] == b'\0\0\0', 'opus-configuration')
            self.pre_skip = int.from_bytes(private[10:12], 'little')
            self.channels = private[9]
            check(self.pre_skip in (120, 312), 'opus-pre-skip')
            check(int.from_bytes(fields.get(0x56aa, b''), 'big')*48000 ==
                  self.pre_skip*1000000000 and
                  int.from_bytes(fields.get(0x56bb, b''), 'big') == 80000000,
                  'opus-delay-configuration')
            sound = self.fields(fields.get(0xe1, b''))
            rate = sound.get(0xb5, b'')
            check(len(rate) in (4, 8), 'opus-rate')
            check(struct.unpack('>f' if len(rate) == 4 else '>d', rate)[0] == 48000 and
                  int.from_bytes(sound.get(0x9f, b''), 'big') == self.channels,
                  'opus-audio-configuration')

    def _block(self, tag, raw, header, block):
        check(len(block) >= 5, 'empty-block')
        w = width(block[0]);check(len(block) > w+3, 'empty-block')
        track = int.from_bytes(block[:w], 'big') & ((1 << (7*w))-1)
        if track == self.audio:
            check(not self.padding_seen, 'audio-after-end-trim')
            check(self.current.clock is not None, 'block-before-clock')
            check(not block[w+2] & 0x7f, 'opus-block-flags')
            duration = packet_samples(block[w+3:])
            check(120 <= duration <= 5760 and duration % 120 == 0,
                  'unsupported-opus-packet-duration')
            ticks = self.current.clock+int.from_bytes(block[w:w+2], 'big', signed=True)
            check(ticks >= 0 and (self.audio_packets or ticks == 0) and
                  abs(ticks*48-self.decoded_samples) <= 48, 'opus-clock')
            padding = 0
            if tag == 0xa0:
                seen = set()
                for key, _, a, z in children(raw, header, len(raw)):
                    check(key in (0xa1, 0x75a2, 0x9b, 0xbf) and key not in seen,
                          'opus-block-group')
                    seen.add(key)
                    if key == 0x75a2:
                        check(0 < z-a <= 8, 'opus-end-trim')
                        padding = int.from_bytes(raw[a:z], 'big', signed=True)
            check(padding >= 0, 'opus-end-trim')
            trim = (padding*48000+500000000)//1000000000
            check(trim < duration and padding in (trim*1000000000//48000,
                  (trim*1000000000+47999)//48000), 'opus-end-trim')
            check(self.audio_packets < self.packet_limit and
                  self.decoded_samples+duration-self.pre_skip <= self.sample_limit+duration-1,
                  'audio-budget')
            # The preceding packet becomes safe only when its successor exists.
            self.indexed_audio_samples = max(0, self.decoded_samples-self.pre_skip)
            self.audio_packets += 1;self.decoded_samples += duration
            self.last_duration = duration;self.durations.add(duration)
            self.end_trim_samples, self.padding_seen = trim, padding > 0
        super()._block(tag, raw, header, block)

    def feed(self, data=b'', *, complete=False):
        output = super().feed(data, complete=complete)
        if complete:
            count = self.decoded_samples-self.pre_skip-self.end_trim_samples
            check(self.audio_packets >= 2 and 649 <= count <= self.sample_limit,
                  'audio-final-count')
            self.audio_samples = self.indexed_audio_samples = count
            self.audio_sample_count_final = True
        return output
