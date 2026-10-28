"""Compare confirmed WebM packet payloads/clocks with FFprobe, without a decoder."""
import csv
import hashlib
import io
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / 'tests'))
from progressive_webm_fixtures import locate


@unittest.skipUnless(shutil.which('g++') and shutil.which('ffmpeg'), 'C++ and FFmpeg required')
class WebMIndexTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='foxicmp-webm-index-')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.root = Path(cls.temp.name)
        cls.exe = cls.root / 'index'
        flags=shlex.split(os.environ.get('FOXICMP_INDEX_TEST_FLAGS','-O2'))
        subprocess.run(['g++', '-std=c++17', *flags, '-I' + str(REPO / 'src'),
                        str(REPO / 'tests/harvester/fragment_index.cpp'), '-o', str(cls.exe)], check=True)
        cls.vorbis_exe=cls.root/'vorbis-headers'
        subprocess.run(['g++','-std=c++17',*flags,'-I'+str(REPO/'src'),str(REPO/'tests/harvester/vorbis_headers_tests.cpp'),'-o',str(cls.vorbis_exe)],check=True)
        cls.paths = []
        for kind in ('video8', 'video10', 'av1video8', 'av1video10', 'audio1', 'audio2', 'vp8video', 'vorbisaudio1', 'vorbisaudio2'):
            path = cls.root / (kind + '.webm')
            args = (['-f', 'lavfi', '-i', 'testsrc2=s=320x180:r=25:d=3', '-c:v', 'libaom-av1' if kind.startswith('av1') else 'libvpx' if kind.startswith('vp8') else 'libvpx-vp9',
                     '-pix_fmt', 'yuv420p10le' if kind.endswith('10') else 'yuv420p',
                     '-threads', '2', '-filter_threads', '1', '-cpu-used', '8', '-g', '25']
                    if 'video' in kind else
                    ['-f', 'lavfi', '-i', 'sine=frequency=880:sample_rate=48000:duration=3',
                     '-ac', kind[-1], '-c:a', 'libvorbis' if kind.startswith('vorbis') else 'libopus'])
            if kind.startswith('av1'):
                # FFmpeg's WebM muxer needs AV1 configuration before its header.
                # Encode to delayed-moov MP4 first, then copy the actual packets.
                encoded=path.with_suffix('.mp4')
                subprocess.run(['ffmpeg','-v','error','-y',*args,'-movflags',
                                '+delay_moov+frag_keyframe+default_base_moof',str(encoded)],check=True)
                args=['-i',str(encoded),'-map','0:0','-c','copy']
            subprocess.run(['ffmpeg', '-v', 'error', '-y', *args, '-f', 'webm',
                            '-cluster_time_limit', '1000', '-live', '1', str(path)], check=True)
            cls.paths.append(path)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def index(self, path, mode='webm', check=True):
        return subprocess.run([str(self.exe), str(path), '1' if 'audio' in path.name else '0', mode],
                              capture_output=True, text=True, check=check)

    def test_packet_payloads_timestamps_and_preroll(self):
        for path in self.paths:
            with self.subTest(path=path.name):
                data = path.read_bytes()
                rows = list(csv.DictReader(io.StringIO(self.index(path).stdout)))
                packets = json.loads(subprocess.check_output([
                    'ffprobe', '-v', 'error', '-show_packets', '-show_data_hash', 'sha256',
                    '-of', 'json', str(path)]))['packets']
                self.assertEqual(len(rows), len(packets))
                for indexed, packet in zip(rows, packets):
                    start, size = int(indexed['offset']), int(indexed['size'])
                    self.assertEqual(size, int(packet['size']))
                    # Matroska FFprobe pos points to the Block header, not the payload.
                    self.assertEqual(hashlib.sha256(data[start:start + size]).hexdigest(),
                                     packet['data_hash'].split(':', 1)[1].lower())
                    # FFprobe quantizes CodecDelay to the 1 ms stream timebase.
                    self.assertAlmostEqual(int(indexed['start']) / 1e7,
                                           float(packet['pts_time']), delta=.000501)
                    self.assertEqual(bool(int(indexed['sync'])), 'K' in packet['flags'])
                    if 'duration_time' in packet:
                        padding=sum(item.get('discard_padding',0) for item in packet.get('side_data_list',[]))/48000
                        self.assertAlmostEqual((int(indexed['end'])-int(indexed['start']))/1e7,float(packet['duration_time'])-padding,delta=.0011)
                if 'audio' in path.name:
                    self.assertEqual(int(rows[0]['start']), 0 if path.name.startswith('vorbis') else -65000)
                    self.assertAlmostEqual(int(rows[-1]['end']) / 1e7, 3, delta=.025 if path.name.startswith('vorbis') else .001)
                    for position in (0, 20000000, -(1 << 63)):
                        first = int(subprocess.check_output([str(self.exe), str(path), '1', 'webm', str(position)]))
                        if position >= 800000:
                            self.assertGreaterEqual(position - int(rows[first]['start']), 800000)
                        else:
                            self.assertEqual(first, 0)

    def test_av1_initial_packet_requires_matching_sequence_and_key_frame(self):
        from progressive_webm_fixtures import width
        source=next(p for p in self.paths if p.name=='av1video8.webm')
        original=source.read_bytes();_,body,stop=locate(original,0xa3)
        at=body+width(original[body])+3;obus={}
        while at<stop:
            header=at;value=original[at];at+=1;kind=(value>>3)&15
            if value&4:at+=1
            size=0
            if value&2:
                for i in range(8):
                    byte=original[at];at+=1;size|=(byte&127)<<(7*i)
                    if byte<128:break
            else:size=stop-at
            obus[kind]=(header,at,at+size);at+=size
        self.assertIn(1,obus);self.assertIn(6,obus)
        for name,at,value in (
            ('missing-sequence',obus[1][0],0x7a),
            ('sequence-mismatch',obus[1][1]+1,original[obus[1][1]+1]^1),
            ('not-key-frame',obus[6][1],original[obus[6][1]]|0x20),
            ('show-existing-frame',obus[6][1],original[obus[6][1]]|0x80),
            ('missing-frame',obus[6][0],0x7a),
            ('forbidden-header',obus[6][0],original[obus[6][0]]|0x80)):
            with self.subTest(name=name):
                data=bytearray(original);data[at]=value
                path=self.root/('av1-'+name+'.webm');path.write_bytes(data)
                self.assertNotEqual(self.index(path,check=False).returncode,0)

    def test_vp8_first_packet_and_vorbis_headers_are_bounded(self):
        from progressive_webm_fixtures import width
        source=next(p for p in self.paths if p.name=='vp8video.webm');original=source.read_bytes();_,body,stop=locate(original,0xa3);start=body+width(original[body])+3
        for name,offset,value in [('inter',0,original[start]|1),('invisible',0,original[start]&~16),('version',0,original[start]|14),('signature',3,0),('width',6,0),('scale',7,original[start+7]|128),('partition',2,255)]:
            with self.subTest(name=name):
                data=bytearray(original);data[start+offset]=value;path=self.root/('vp8-'+name+'.webm');path.write_bytes(data);self.assertNotEqual(self.index(path,check=False).returncode,0)
        source=next(p for p in self.paths if p.name=='vorbisaudio2.webm');original=source.read_bytes();_,body,stop=locate(original,0x63a2)
        id_start=original.index(b'\x01vorbis',body,stop);setup_start=original.index(b'\x05vorbis',body,stop)
        for name,offset,value in [('count',body,1),('id-version',id_start+7,1),('channels',id_start+11,6),('rate',id_start+12,0),('block',id_start+28,0),('framing',id_start+29,0),('setup-sync',setup_start+8,0),('last-framing',stop-1,0)]:
            with self.subTest(name=name):
                data=bytearray(original);data[offset]=value;path=self.root/('vorbisaudio-'+name+'.webm');path.write_bytes(data);self.assertNotEqual(self.index(path,check=False).returncode,0)
        _,body,stop=locate(original,0xa3);packet=body+width(original[body])+3
        data=bytearray(original);data[packet]|=1;path=self.root/'vorbisaudio-header-packet.webm';path.write_bytes(data);self.assertNotEqual(self.index(path,check=False).returncode,0)

    def test_vorbis_configuration_truncation_mutations(self):
        for source in self.paths:
            if not source.name.startswith('vorbisaudio'):continue
            data=source.read_bytes();_,body,end=locate(data,0x63a2);private=self.root/(source.stem+'.private');private.write_bytes(data[body:end])
            subprocess.run([str(self.vorbis_exe),str(private)],check=True)

    def test_schema_one_and_fmp4_flags_do_not_admit_webm(self):
        for path in self.paths:
            for mode in ('', 'hevc', 'av1', 'vp9', 'opus'):
                with self.subTest(path=path.name, mode=mode):
                    self.assertNotEqual(self.index(path, mode, check=False).returncode, 0)

    def test_damaged_packets_and_incomplete_final_cluster_are_rejected(self):
        video = self.paths[0].read_bytes()
        mutations = {}
        mutations['truncated'] = video[:-1]
        for name, change in [('lacing', lambda d, at: d.__setitem__(at + 3, d[at + 3] | 2)),
                             ('other-track', lambda d, at: d.__setitem__(at, 0x82)),
                             ('not-keyframe', lambda d, at: d.__setitem__(at + 3, d[at + 3] & 127))]:
            data = bytearray(video)
            _, at, _ = locate(data, 0xa3)
            change(data, at)
            mutations[name] = data
        for name, data in mutations.items():
            with self.subTest(name=name):
                path = self.root / (name + '.webm')
                path.write_bytes(data)
                self.assertNotEqual(self.index(path, check=False).returncode, 0)


if __name__ == '__main__':
    unittest.main()
