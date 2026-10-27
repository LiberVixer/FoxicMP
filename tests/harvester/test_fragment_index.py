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
    def test_hevc_packet_clocks_and_schema_one_codec_boundary(self):
        for depth,tag in [(8,'hvc1'),(10,'hev1')]:
            with self.subTest(depth=depth,tag=tag):
                path=self.root/('hevc'+str(depth)+'.mp4')
                subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i','testsrc2=s=320x180:r=25:d=4',
                    '-c:v','libx265','-pix_fmt','yuv420p' if depth==8 else 'yuv420p10le','-threads','2','-filter_threads','1',
                    '-x265-params','pools=1:frame-threads=1:log-level=error:keyint=25:min-keyint=25:scenecut=0',
                    '-tag:v',tag,'-movflags','+empty_moov+frag_keyframe+default_base_moof',str(path)],check=True)
                rejected=subprocess.run([str(self.exe),str(path),'0'],capture_output=True,text=True)
                self.assertNotEqual(rejected.returncode,0)
                self.assertIn('Unsupported fragment sample entry',rejected.stderr)
                accepted=subprocess.run([str(self.exe),str(path),'0','hevc'],capture_output=True,text=True,check=True)
                rows=list(csv.DictReader(io.StringIO(accepted.stdout)))
                packets=json.loads(subprocess.check_output(['ffprobe','-v','quiet','-show_packets','-of','json',str(path)]))['packets']
                self.assertEqual(len(rows),len(packets))
                for indexed,packet in zip(rows,packets):
                    self.assertEqual(int(indexed['offset']),int(packet['pos']))
                    self.assertEqual(int(indexed['size']),int(packet['size']))
                    self.assertAlmostEqual(int(indexed['start'])/1e7,float(packet['pts_time']),delta=.000001)
                    self.assertAlmostEqual(int(indexed['decode'])/1e7,float(packet['dts_time']),delta=.000001)
                    self.assertEqual(bool(int(indexed['sync'])),'K' in packet['flags'])
    def test_av1_packet_clocks_and_schema_one_codec_boundary(self):
        for depth in (8,10):
            with self.subTest(depth=depth):
                path=self.root/('av1-'+str(depth)+'.mp4')
                subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i','testsrc2=s=320x180:r=25:d=3',
                    '-c:v','libaom-av1','-pix_fmt','yuv420p' if depth==8 else 'yuv420p10le','-threads','2','-filter_threads','1',
                    '-cpu-used','8','-row-mt','1','-g','25','-lag-in-frames','25','-auto-alt-ref','1',
                    '-movflags','+delay_moov+frag_keyframe+default_base_moof',str(path)],check=True)
                for rejected_args in ([str(self.exe),str(path),'0'],[str(self.exe),str(path),'0','hevc']):
                    rejected=subprocess.run(rejected_args,capture_output=True,text=True)
                    self.assertNotEqual(rejected.returncode,0)
                    self.assertIn('Unsupported fragment sample entry',rejected.stderr)
                accepted=subprocess.run([str(self.exe),str(path),'0','av1'],capture_output=True,text=True,check=True)
                rows=list(csv.DictReader(io.StringIO(accepted.stdout)))
                packets=json.loads(subprocess.check_output(['ffprobe','-v','quiet','-show_packets','-of','json',str(path)]))['packets']
                self.assertEqual(len(rows),len(packets))
                for indexed,packet in zip(rows,packets):
                    self.assertEqual(int(indexed['offset']),int(packet['pos']))
                    self.assertEqual(int(indexed['size']),int(packet['size']))
                    self.assertAlmostEqual(int(indexed['start'])/1e7,float(packet['pts_time']),delta=.000001)
                    self.assertAlmostEqual(int(indexed['decode'])/1e7,float(packet['dts_time']),delta=.000001)
                    self.assertEqual(bool(int(indexed['sync'])),'K' in packet['flags'])
    def test_vp9_packet_clocks_and_schema_one_codec_boundary(self):
        for depth in (8,10):
            with self.subTest(depth=depth):
                path=self.root/('vp9-'+str(depth)+'.mp4')
                subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i','testsrc2=s=320x180:r=25:d=3',
                    '-c:v','libvpx-vp9','-pix_fmt','yuv420p' if depth==8 else 'yuv420p10le','-threads','2','-filter_threads','1',
                    '-deadline','good','-cpu-used','4','-row-mt','1','-g','25','-lag-in-frames','25','-auto-alt-ref','1',
                    '-movflags','+delay_moov+frag_keyframe+default_base_moof',str(path)],check=True)
                for rejected_args in ([str(self.exe),str(path),'0'],[str(self.exe),str(path),'0','hevc'],[str(self.exe),str(path),'0','av1']):
                    rejected=subprocess.run(rejected_args,capture_output=True,text=True)
                    self.assertNotEqual(rejected.returncode,0)
                    self.assertIn('Unsupported fragment sample entry',rejected.stderr)
                accepted=subprocess.run([str(self.exe),str(path),'0','vp9'],capture_output=True,text=True,check=True)
                rows=list(csv.DictReader(io.StringIO(accepted.stdout)))
                packets=json.loads(subprocess.check_output(['ffprobe','-v','quiet','-show_packets','-of','json',str(path)]))['packets']
                self.assertEqual(len(rows),len(packets))
                for indexed,packet in zip(rows,packets):
                    self.assertEqual(int(indexed['offset']),int(packet['pos']))
                    self.assertEqual(int(indexed['size']),int(packet['size']))
                    self.assertAlmostEqual(int(indexed['start'])/1e7,float(packet['pts_time']),delta=.000001)
                    self.assertAlmostEqual(int(indexed['decode'])/1e7,float(packet['dts_time']),delta=.000001)
                    self.assertEqual(bool(int(indexed['sync'])),'K' in packet['flags'])
    def test_opus_clocks_priming_and_seek_preroll(self):
        for channels in (1,2):
            with self.subTest(channels=channels):
                path=self.root/('opus-'+str(channels)+'.mp4')
                subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i','sine=frequency=880:sample_rate=48000:duration=4',
                    '-ac',str(channels),'-c:a','libopus','-movflags','+delay_moov+frag_keyframe+default_base_moof',
                    '-frag_duration','1000000',str(path)],check=True)
                for mode in ('','hevc','av1','vp9'):
                    rejected=subprocess.run([str(self.exe),str(path),'1',mode],capture_output=True,text=True)
                    self.assertNotEqual(rejected.returncode,0);self.assertIn('Unsupported fragment sample entry',rejected.stderr)
                accepted=subprocess.run([str(self.exe),str(path),'1','opus'],capture_output=True,text=True,check=True)
                rows=list(csv.DictReader(io.StringIO(accepted.stdout)))
                packets=json.loads(subprocess.check_output(['ffprobe','-v','quiet','-show_packets','-of','json',str(path)]))['packets']
                self.assertEqual(len(rows),len(packets));self.assertEqual(int(rows[0]['start']),-65000)
                self.assertEqual(int(rows[-1]['end']),40000000)
                for indexed,packet in zip(rows,packets):
                    self.assertEqual(int(indexed['offset']),int(packet['pos']));self.assertEqual(int(indexed['size']),int(packet['size']))
                    self.assertAlmostEqual(int(indexed['start'])/1e7,float(packet['pts_time']),delta=.000001)
                    self.assertAlmostEqual(int(indexed['decode'])/1e7,float(packet['dts_time']),delta=.000001)
                    self.assertTrue(bool(int(indexed['sync'])))
                for position in (0,500000,11510000,20000000):
                    first=int(subprocess.check_output([str(self.exe),str(path),'1','opus',str(position)]))
                    target=max(0,position-800000)
                    self.assertLessEqual(int(rows[first]['start']),target)
                    self.assertGreater(int(rows[first]['end']),target)
                    if position>=800000:self.assertGreaterEqual(position-int(rows[first]['start']),800000)
                # A trimmed stream can have >80 ms of real packets before timeline zero.
                # Seeking to zero must retain that preroll instead of clamping to zero.
                data=bytearray(path.read_bytes());at=data.index(b'dOps');struct.pack_into('>H',data,at+6,4800)
                at=data.index(b'elst');struct.pack_into('>I',data,at+16,4800)
                trimmed=path.with_name('trimmed-'+path.name);trimmed.write_bytes(data)
                csv_text=subprocess.check_output([str(self.exe),str(trimmed),'1','opus'],text=True)
                trimmed_rows=list(csv.DictReader(io.StringIO(csv_text)))
                first=int(subprocess.check_output([str(self.exe),str(trimmed),'1','opus','0']))
                self.assertLessEqual(int(trimmed_rows[first]['start']),-800000)
                self.assertGreater(int(trimmed_rows[first]['end']),-800000)
                first=int(subprocess.check_output([str(self.exe),str(trimmed),'1','opus',str(-(1<<63))]))
                self.assertEqual(first,0)
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
