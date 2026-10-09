"""Compare incremental fragment indexes with actual FFprobe packets, and reject damage."""
import csv
import io
import json
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO=Path(__file__).resolve().parents[2]
@unittest.skipUnless(shutil.which('g++') and shutil.which('ffmpeg'),'C++ compiler and FFmpeg required')
class FragmentTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.root=Path(cls.temp.name);cls.exe=cls.root/'index'
        subprocess.run(['g++','-std=c++17','-O2','-I'+str(REPO/'src'),str(REPO/'tests/harvester/fragment_index.cpp'),'-o',str(cls.exe)],check=True)
        for kind in ['video','audio']:
            path=cls.root/(kind+'.mp4');input_args=['-f','lavfi','-i','testsrc2=s=320x180:r=25:d=8','-c:v','libx264','-g','50','-bf','2'] if kind=='video' else ['-f','lavfi','-i','sine=frequency=880:duration=8','-c:a','aac']
            subprocess.run(['ffmpeg','-v','error','-y',*input_args,'-movflags','+empty_moov+frag_keyframe+default_base_moof','-frag_duration','2000000',str(path)],check=True)
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    def test_all_packet_ranges_and_clocks_match_ffprobe(self):
        for kind in ['video','audio']:
            with self.subTest(kind=kind):
                p=self.root/(kind+'.mp4');r=subprocess.run([str(self.exe),str(p),'0' if kind=='video' else '1'],capture_output=True,text=True,check=True)
                rows=list(csv.DictReader(io.StringIO(r.stdout)))
                packets=json.loads(subprocess.check_output(['ffprobe','-v','quiet','-show_packets','-of','json',str(p)]))['packets']
                self.assertEqual(len(rows),len(packets))
                for a,b in zip(rows,packets):
                    self.assertEqual(int(a['offset']),int(b['pos']));self.assertEqual(int(a['size']),int(b['size']))
                    self.assertAlmostEqual(int(a['start'])/1e7,float(b['pts_time']),delta=.000001)
                    self.assertAlmostEqual(int(a['decode'])/1e7,float(b['dts_time']),delta=.000001)
    def test_incomplete_final_packet_is_rejected(self):
        data=(self.root/'video.mp4').read_bytes();position=0;end=0
        while position<len(data):
            n,kind=struct.unpack_from('>I4s',data,position)
            if kind==b'mdat':end=position+n
            position+=n
        p=self.root/'truncated.mp4';p.write_bytes(data[:end-1])
        r=subprocess.run([str(self.exe),str(p),'0'],capture_output=True,text=True)
        self.assertNotEqual(r.returncode,0);self.assertIn('Truncated final',r.stderr)
    def test_trun_cannot_publish_out_of_payload_sample(self):
        data=bytearray((self.root/'video.mp4').read_bytes());at=data.index(b'trun')
        # data_offset is the first optional field after full-box/sample_count.
        struct.pack_into('>i',data,at+12,0x7fffffff)
        p=self.root/'bad-offset.mp4';p.write_bytes(data)
        r=subprocess.run([str(self.exe),str(p),'0'],capture_output=True,text=True)
        self.assertNotEqual(r.returncode,0);self.assertIn('outside confirmed',r.stderr)

if __name__=='__main__':unittest.main()
