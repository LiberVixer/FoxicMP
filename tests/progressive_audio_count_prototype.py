"""TEST ONLY: derive bounded Opus presentation samples without an audio plan.

Uses the existing incremental SimpleBlock study for EBML/confirmed-byte framing.
No player/SDK imports this module. Video frames/endpoint remain supplied to that
study. Audio packet bytes, not file size or an external count, drive this clock.
"""
from progressive_simple_vfr_prototype import SimpleVfrPrototype, check
from progressive_webm_fixtures import width
from progressive_webm_opus_remux_preflight import children


def packet_samples(packet):
    """Validate Opus framing and count samples; bounded equivalent of TOC rules."""
    check(bool(packet), 'empty-opus-packet')
    config, code = packet[0] >> 3, packet[0] & 3
    frame = ((120 << (config & 3)) if config >= 16 else
             (480 << (config & 1)) if config >= 12 else
             (480, 960, 1920, 2880)[config & 3])
    at, end, count, vbr = 1, len(packet), 1 if code == 0 else 2, code == 2
    if code == 3:
        check(at < end, 'truncated-opus-count')
        flags = packet[at];at += 1
        count, vbr = flags & 63, bool(flags & 128)
        check(0 < count <= 48, 'invalid-opus-count')
        if flags & 64:
            padding = 0
            while True:
                check(at < end, 'truncated-opus-padding')
                value = packet[at];at += 1
                padding += 254 if value == 255 else value
                check(padding <= end-at, 'opus-padding-overrun')
                if value != 255:break
            end -= padding
    check(frame*count <= 5760, 'opus-duration-limit')
    if vbr:
        total = 0
        for _ in range(1, count):
            check(at < end, 'truncated-opus-frame-length')
            size = packet[at];at += 1
            if size >= 252:
                check(at < end, 'truncated-opus-frame-length')
                size += 4*packet[at];at += 1
            check(size <= 1275 and total+size <= end-at, 'opus-frame-overrun')
            total += size
        check(total <= end-at and end-at-total <= 1275, 'opus-final-frame-overrun')
    else:
        check(at <= end and (end-at) % count == 0 and
              (end-at)//count <= 1275, 'opus-cbr-framing')
    return frame*count


class AudioCountPrototype(SimpleVfrPrototype):
    """Known length/endpoint, zero-origin, 960-sample packets, pre-skip312.

    indexed_audio_samples excludes the latest packet until the next audio packet
    or confirmed writer EOF. Padding alone never means complete. The proposal
    deliberately retains the existing >=2 packets / >=649 presentation samples.
    """
    def __init__(self, length, frames, end100ns, *, sample_limit=48000000,
                 packet_limit=50000):
        super().__init__(length, frames, end100ns)
        check(649 <= sample_limit <= 48000000, 'audio-sample-limit')
        check(2 <= packet_limit <= 50000, 'audio-packet-limit')
        self.sample_limit, self.packet_limit = sample_limit, packet_limit
        self.audio_packets = self.decoded_samples = self.indexed_audio_samples = 0
        self.end_trim_samples = 0
        self.audio_samples = None
        self.audio_sample_count_final = False
        self.padding_seen = False

    def _configuration(self, body):
        super()._configuration(body)
        for tag, _, start, stop in children(body, 0, len(body)):
            if tag != 0xae:continue
            fields = {}
            for key, _, a, z in children(body, start, stop):
                check(key not in fields, 'duplicate-track-field')
                fields[key] = body[a:z]
            if int.from_bytes(fields[0x83], 'big') != 2:continue
            private = fields.get(0x63a2, b'')
            check(len(private) == 19 and private[:9] == b'OpusHead\x01' and
                  private[9] in (1, 2) and private[10:12] == b'8\x01' and
                  private[16:] == b'\0\0\0', 'opus-configuration')
            check(int.from_bytes(fields.get(0x56aa, b''), 'big') == 6500000 and
                  int.from_bytes(fields.get(0x56bb, b''), 'big') == 80000000,
                  'opus-delay-configuration')

    def _block(self, tag, raw, header, block):
        check(len(block) >= 5, 'empty-block')
        w = width(block[0])
        check(len(block) > w+3, 'empty-block')
        track = int.from_bytes(block[:w], 'big') & ((1 << (7*w))-1)
        if track == self.audio:
            check(not self.padding_seen, 'audio-after-end-trim')
            check(self.current.clock is not None, 'block-before-clock')
            check(not block[w+2] & 0x7f, 'opus-block-flags')
            samples = packet_samples(block[w+3:])
            check(samples == 960, 'unsupported-opus-packet-duration')
            ticks = self.current.clock+int.from_bytes(block[w:w+2], 'big', signed=True)
            check(ticks >= 0 and (self.audio_packets or ticks == 0) and
                  abs(ticks*48-self.decoded_samples) <= 48, 'opus-clock')
            padding = 0
            if tag == 0xa0:
                body = raw[header:];seen = set()
                for key, _, a, z in children(body, 0, len(body)):
                    check(key in (0xa1, 0x75a2, 0x9b, 0xbf) and key not in seen,
                          'opus-block-group')
                    seen.add(key)
                    if key == 0x75a2:
                        check(0 < z-a <= 8, 'opus-end-trim')
                        padding = int.from_bytes(body[a:z], 'big', signed=True)
            check(padding >= 0, 'opus-end-trim')
            trim = (padding*48000+500000000)//1000000000
            check(trim < 960 and padding in (trim*1000000000//48000,
                  (trim*1000000000+47999)//48000), 'opus-end-trim')
            # Commit the preceding packet only after seeing its successor.
            self.indexed_audio_samples = max(0, self.decoded_samples-312)
            self.decoded_samples += samples;self.audio_packets += 1
            check(self.audio_packets <= self.packet_limit and
                  self.decoded_samples-312 <= self.sample_limit+959,
                  'audio-budget')
            self.end_trim_samples, self.padding_seen = trim, padding > 0
        super()._block(tag, raw, header, block)

    def feed(self, data=b'', *, complete=False):
        output = super().feed(data, complete=complete)
        if complete:
            count = self.decoded_samples-312-self.end_trim_samples
            check(self.audio_packets >= 2 and 649 <= count <= self.sample_limit,
                  'audio-final-count')
            self.audio_samples = self.indexed_audio_samples = count
            self.audio_sample_count_final = True
        return output
