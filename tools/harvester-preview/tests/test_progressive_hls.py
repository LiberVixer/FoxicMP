"""Actual native HLS requests, cached-data preview, and ordinary final remux."""
import http.server
import json
import os
import shutil
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest.mock import patch

@unittest.skipUnless(shutil.which('ffmpeg'),'FFmpeg required')
class HlsTests(unittest.TestCase):
    def test_failed_final_preview_publication_returns_failure(self):
        # A corrupt/truncated preview must not escape into the ordinary downloader.
        from unittest.mock import Mock
        from scripts.progressive_hls import HlsPreview
        preview=HlsPreview.__new__(HlsPreview)
        preview.condition=threading.Condition();preview.feeder=Mock();preview.feeder.is_alive.return_value=False
        preview.process=Mock();preview.process.wait.return_value=0
        preview.halt=threading.Event();preview.monitor=Mock();preview.error=None
        preview._publish=Mock(side_effect=ValueError('Unclosed final HLS preview fragment'))
        preview.abort=Mock()
        self.assertFalse(preview.finish())
        preview.abort.assert_called_once_with()
        self.assertIsInstance(preview.error,ValueError)

    def test_cached_hls_preview_no_second_requests_same_coded_streams(self):
        import yt_dlp
        from scripts.progressive_download import make_downloader_class
        from scripts.progressive_session import Session,atomic_json,read_json
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);media=root/'http';media.mkdir()
            subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i','testsrc2=s=320x180:r=25',
                            '-f','lavfi','-i','sine=frequency=880:sample_rate=48000','-t','20',
                            '-c:v','libx264','-preset','ultrafast','-g','50','-bf','2','-c:a','aac',
                            '-f','hls','-hls_time','2','-hls_list_size','0','-hls_segment_filename',str(media/'chunk%03d.ts'),str(media/'index.m3u8')],check=True)
            requested=[];early=[];all_segments={p.name for p in media.glob('*.ts')}
            class Handler(http.server.BaseHTTPRequestHandler):
                def do_GET(self):
                    requested.append(self.path);path=media/self.path.lstrip('/')
                    data=path.read_bytes();self.send_response(200);self.send_header('Content-Length',str(len(data)));self.end_headers()
                    for p in range(0,len(data),16384):
                        self.wfile.write(data[p:p+16384]);self.wfile.flush();time.sleep(.015)
                def log_message(self,*args):pass
            server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
            threading.Thread(target=server.serve_forever,daemon=True).start()
            self.addCleanup(server.server_close);self.addCleanup(server.shutdown)
            base=f'http://127.0.0.1:{server.server_port}'
            original=subprocess.Popen;progress=Session.progress
            def launch(command,*args,**kwargs):
                if '--provider' in command:
                    manifest=Path(command[command.index('--provider')+1]);atomic_json(manifest.parent/'provider.json',{'ready':True})
                    class Fake:
                        def poll(self):return None
                    return Fake()
                return original(command,*args,**kwargs)
            def updated(session,*args,**kwargs):
                progress(session,*args,**kwargs)
                if session.ready() and len([p for p in requested if p.endswith('.ts')])<len(all_segments):early.append(True)
            info={'id':'local-hls','title':'HLS test','extractor':'test','extractor_key':'Test','webpage_url':base,
                  'url':base+'/index.m3u8','format_id':'hls','protocol':'m3u8_native','ext':'mp4','duration':20,
                  'vcodec':'avc1.64001e','acodec':'mp4a.40.2','height':180}
            with patch('scripts.progressive_download.subprocess.Popen',side_effect=launch),patch.object(Session,'progress',updated),patch.dict(os.environ,{'YTD_TEMP_DIR':str(root)}):
                with make_downloader_class(yt_dlp)({'outtmpl':str(root/'finished.mp4'),'quiet':True,'noprogress':True,'download_archive':str(root/'archive.txt')}) as ydl:ydl.process_ie_result(info,download=True)
            self.assertTrue(early,'Preview did not become ready before native HLS finished: '+repr([(str(p),p.read_text(errors='replace')) for p in (root/'foxicmp-sessions').glob('*/preview-ffmpeg.log')]))
            self.assertEqual(requested.count('/index.m3u8'),1)
            self.assertEqual(sorted(p.lstrip('/') for p in requested if p.endswith('.ts')),sorted(all_segments))
            manifest=next((root/'foxicmp-sessions').glob('*/session.json'));state=read_json(manifest)
            self.assertEqual(state['state'],'complete');self.assertEqual(state['timestampOffset100ns'],10000000)
            self.assertTrue(all(t.get('fragmented') and t['state']=='complete' for t in state['tracks'].values()))
            self.assertIn('local-hls',(root/'archive.txt').read_text())
            for kind,selector in [('video','v:0'),('audio','a:0')]:
                original_track=manifest.parent/state['tracks'][kind]['path']
                metadata=json.loads(subprocess.check_output(['ffprobe','-v','quiet','-show_streams','-of','json',str(original_track)]))['streams'][0]
                if kind=='audio':
                    self.assertEqual(metadata['profile'],'LC')
                    self.assertGreater(metadata.get('extradata_size',0),0,'AAC initialization missing from early moov')
                hashes=[]
                for path,stream in [(original_track,'0:0'),(root/'finished.mp4','0:'+selector)]:
                    hashes.append(subprocess.check_output(['ffmpeg','-v','error','-i',str(path),'-map',stream,'-c','copy','-f','hash','-hash','sha256','-'],text=True))
                self.assertEqual(hashes[0],hashes[1])
                indexes=[]
                for path,select in [(original_track,'0:0'),(root/'finished.mp4','0:'+selector)]:
                    raw=subprocess.check_output(['ffprobe','-v','quiet','-select_streams','a:0' if kind=='audio' else 'v:0','-show_packets','-of','json',str(path)])
                    indexes.append(json.loads(raw)['packets'])
                self.assertEqual(len(indexes[0]),len(indexes[1]))
                for preview,final in zip(*indexes):
                    for timestamp in ['pts_time','dts_time']:
                        self.assertAlmostEqual(float(preview[timestamp])-1,float(final[timestamp]),delta=.001)

if __name__=='__main__':unittest.main()
