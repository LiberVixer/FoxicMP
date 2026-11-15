"""TEST ONLY: bounded incremental SimpleBlock -> explicit-duration WebM study.

No player capability uses this class. feed() sees only newly confirmed bytes;
no complete-file layout/oracle is available to the parser. Output is drained by
the caller. One unresolved video packet stays unpublished until next PTS/EOF.
"""
from collections import deque
from dataclasses import dataclass, field

from progressive_webm_fixtures import width
from progressive_webm_opus_remux_preflight import children
from progressive_webm_vfr_preflight import ebml, unsigned


class Invalid(ValueError):
    pass


def check(value, code):
    if not value:
        raise Invalid(code)


@dataclass
class Cluster:
    start: int
    end: object
    body: bytearray = field(default_factory=bytearray)
    closed: bool = False
    clock: object = None
    children: int = 0
    blocks: int = 0


class SimpleVfrPrototype:
    def __init__(self, length, frames, end100ns, *, pending_limit=32*1024*1024):
        check(type(length) is int and 0 < length <= 128*1024*1024, 'input-limit')
        check(type(frames) is int and 0 < frames <= 50000, 'frame-plan')
        check(type(end100ns) is int and 0 < end100ns <= 36000000000 and end100ns % 10000 == 0, 'end-plan')
        check(0 < pending_limit <= 32*1024*1024, 'pending-limit')
        self.length, self.frames, self.end_ms = length, frames, end100ns//10000
        self.limit = pending_limit
        self.buffer = bytearray();self.supplied = self.at = 0
        self.phase = 'ebml';self.head = bytearray();self.head_ready = False
        self.video = self.audio = None;self.seen = self.media = 0
        self.info = self.tracks = False
        self.queue = deque();self.current = self.pending = None
        self.clusters = self.elements = self.dropped = 0
        self.peak_pending = self.peak_input = 0
        self.finished = False;self.waiting = 'header'

    def _header(self):
        b = self.buffer
        if not b:return None
        ids = width(b[0], 4)
        if len(b) <= ids:return None
        size_width = width(b[ids])
        h = ids+size_width
        if len(b) < h:return None
        tag = int.from_bytes(b[:ids], 'big')
        size = int.from_bytes(b[ids:h], 'big') & ((1 << (7*size_width))-1)
        return tag, h, None if size == (1 << (7*size_width))-1 else size

    def _take(self, n):
        value = bytes(self.buffer[:n]);del self.buffer[:n];self.at += n
        check(self.at <= self.supplied <= self.length, 'read-unconfirmed')
        return value

    def _budget(self):
        n = sum(len(c.body) for c in self.queue)+(len(self.pending[2]) if self.pending else 0)
        self.peak_pending = max(self.peak_pending, n)
        check(n <= self.limit and len(self.queue) <= 32, 'pending-limit')
        check(all(len(c.body) <= 16*1024*1024 for c in self.queue), 'cluster-limit')

    def _resolve(self, end):
        c, at, block, start = self.pending
        duration = end-start
        check(0 < duration <= 1000 and end <= self.end_ms, 'video-clock')
        w = width(block[0]);key = bool(block[w+2] & 0x80)
        body = bytearray(block);body[w+2] &= 0x7f
        group = ebml(0xa1, body)+unsigned(0x9b, duration)
        if not key:group += ebml(0xfb, b'\0')
        c.body[at:at] = ebml(0xa0, group)
        self.pending = None;self._budget()

    def _flush(self, output):
        while self.queue and self.queue[0].closed and (not self.pending or self.pending[0] is not self.queue[0]):
            c = self.queue.popleft();output.append(ebml(0x1f43b675, c.body))

    def _close_cluster(self):
        check(self.current.clock is not None and self.current.blocks > 0, 'empty-cluster')
        self.current.closed = True;self.current = None;self.phase = 'top'

    def _block(self, tag, raw, h, body):
        c = self.current
        check(c.clock is not None and len(body) >= 5, 'block-before-clock')
        w = width(body[0]);check(len(body) > w+3, 'empty-block')
        track = int.from_bytes(body[:w], 'big') & ((1 << (7*w))-1)
        check(track in (self.video, self.audio), 'foreign-track')
        self.media += 1;check(self.media <= 50000, 'packet-limit')
        c.blocks += 1
        if track == self.video:
            check(tag == 0xa3 and not (body[w+2] & 0x7f), 'simple-video-only')
            pts = c.clock+int.from_bytes(body[w:w+2], 'big', signed=True)
            check(0 <= pts < self.end_ms and self.seen < self.frames, 'video-clock')
            if not self.seen:check(pts == 0 and body[w+2] & 0x80, 'initial-keyframe')
            if self.pending:self._resolve(pts)
            self.seen += 1;self.pending = (c, len(c.body), bytes(body), pts)
        else:
            c.body += raw
        self._budget()

    def _configuration(self, body):
        ids = []
        for tag, _, a, z in children(body, 0, len(body)):
            if tag in (0xec, 0xbf):continue
            check(tag == 0xae, 'track-entry')
            fields = {t:body[x:y] for t, _, x, y in children(body, a, z)}
            kind = int.from_bytes(fields[0x83], 'big');track = int.from_bytes(fields[0xd7], 'big')
            check(kind in (1, 2) and 0 < track <= 0xffffffff, 'track-kind')
            check(fields[0x86] == (b'V_VP9' if kind == 1 else b'A_OPUS'), 'codec')
            check(track not in ids, 'duplicate-track');ids.append(track)
            if kind == 1:
                check(self.video is None and 0x23e383 not in fields, 'default-duration-forbidden')
                self.video = track
            else:
                check(self.audio is None, 'duplicate-track');self.audio = track
        check(len(ids) == 2 and self.video is not None and self.audio is not None, 'two-tracks')

    def feed(self, data=b'', *, complete=False):
        check(not self.finished and len(data) <= 65536, 'feed-state')
        check(self.supplied+len(data) <= self.length, 'input-length')
        self.supplied += len(data);self.buffer += data
        self.peak_input = max(self.peak_input, len(self.buffer))
        check(len(self.buffer) <= 16*1024*1024+65548, 'input-buffer-limit')
        check(not complete or self.supplied == self.length, 'truncated-source')
        output = []
        while True:
            if self.phase == 'cluster' and self.current.end is not None and self.at == self.current.end:
                self._close_cluster();self._flush(output)
                continue
            header = self._header()
            if header is None:
                if complete:
                    check(not self.buffer and self.at == self.length and self.phase in ('top', 'cluster'), 'truncated-element')
                    if self.current:
                        check(self.current.end is None, 'truncated-cluster')
                        self._close_cluster()
                    check(self.seen == self.frames and self.pending is not None, 'incomplete-frame-count')
                    self._resolve(self.end_ms);self._flush(output)
                    check(not self.queue, 'unresolved-cluster');self.finished = True;self.waiting = ''
                else:self.waiting = 'source-complete' if self.at == self.length else 'element-header'
                break
            tag, h, size = header
            if self.phase == 'segment':
                check(tag == 0x18538067 and size is None, 'unknown-segment-required')
                self.head += self._take(h);self.phase = 'top';continue
            if self.phase == 'cluster' and self.current.end is None and tag in (0x1f43b675, 0x1c53bb6b, 0x1254c367):
                self._close_cluster();self._flush(output)
                continue
            if self.phase == 'top' and tag == 0x1f43b675:
                check(self.info and self.tracks, 'head-not-ready')
                if not self.head_ready:output.append(bytes(self.head));self.head.clear();self.head_ready = True
                self.clusters += 1;check(self.clusters <= 8192, 'cluster-count')
                check(size is None or size <= 16*1024*1024, 'cluster-limit')
                self.current = Cluster(self.at, None if size is None else self.at+h+size)
                self._take(h);self.queue.append(self.current);self.phase = 'cluster';self._budget();continue
            limit = 4096 if self.phase == 'ebml' else 16*1024*1024 if self.phase == 'cluster' else 1048576
            check(size is not None and size <= limit, 'element-limit')
            if self.phase == 'cluster':
                check(self.current.end is None or self.at+h+size <= self.current.end, 'child-exceeds-cluster')
                check(self.at+h+size-self.current.start <= 16*1024*1024, 'cluster-limit')
            if len(self.buffer) < h+size:
                check(not complete, 'truncated-element');self.waiting = 'element-body';break
            raw = self._take(h+size);body = raw[h:]
            if not self.head_ready:check(self.at <= 1048576, 'head-limit')
            if self.phase == 'ebml':
                check(tag == 0x1a45dfa3, 'doctype')
                fields = {t:body[a:z] for t, _, a, z in children(body, 0, len(body))}
                check(fields[0x4282] == b'webm', 'doctype');self.head += raw;self.phase = 'segment'
            elif self.phase == 'top':
                self.elements += 1;check(self.elements <= 32768, 'element-count')
                if tag in (0x114d9b74, 0x1c53bb6b, 0xbf):self.dropped += 1;continue
                check(not self.head_ready and tag in (0x1549a966, 0x1654ae6b, 0x1254c367, 0xec), 'top-level-element')
                if tag == 0x1549a966:
                    check(not self.info, 'duplicate-info');self.info = True
                    fields = {t:body[a:z] for t, _, a, z in children(body, 0, len(body))}
                    check(int.from_bytes(fields.get(0x2ad7b1, b'\x0f\x42\x40'), 'big') == 1000000, 'timestamp-scale')
                if tag == 0x1654ae6b:
                    check(not self.tracks, 'duplicate-tracks');self.tracks = True;self._configuration(body)
                self.head += raw;check(len(self.head) <= 1048576, 'head-limit')
            else:
                c = self.current;c.children += 1;check(c.children <= 65536, 'cluster-children')
                if tag == 0xe7:
                    check(c.clock is None and 0 < len(body) <= 8, 'cluster-clock')
                    c.clock = int.from_bytes(body, 'big');check(c.clock <= 3600000, 'cluster-clock');c.body += raw
                elif tag == 0xa3:self._block(tag, raw, h, body)
                elif tag == 0xa0:
                    fields = [body[a:z] for t, _, a, z in children(body, 0, len(body)) if t == 0xa1]
                    check(len(fields) == 1, 'audio-block-group');block = fields[0]
                    w = width(block[0]);track = int.from_bytes(block[:w], 'big') & ((1 << (7*w))-1)
                    check(track == self.audio, 'simple-video-only');self._block(tag, raw, h, block)
                elif tag in (0xbf, 0xa7, 0xab):self.dropped += 1
                else:
                    check(tag == 0xec, 'cluster-element');c.body += raw
                self._budget();self._flush(output)
        return b''.join(output)
