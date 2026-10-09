"""Reject absent/bad signals and check known A/V offsets independently of player."""
import tempfile
import unittest
from pathlib import Path
from windows_av import analyze

class AnalysisTests(unittest.TestCase):
    def capture(self,root,offset=0,flags=0,missing=False):
        p=Path(root)/'capture.csv'
        video=[];audio=[]
        # Ground truth: six flashes/beeps spaced 2s, sampled at 10ms/1ms.
        for i in range(1400):
            t=i*100000
            high=20000000<=t<=120000000 and t%20000000<800000
            video.append(f'v,{t+1},{t+10001},{255 if high else 0},0\n')
        for i in range(14000):
            t=i*10000
            origin=t-offset*10000
            high=not missing and 20000000<=origin<=120000000 and origin%20000000<800000
            audio.append(f'a,{t+1},{t+10001},{5000 if high else 0},{flags}\n')
        p.write_text(''.join(video));Path(str(p)+'.audio').write_text(''.join(audio))
        return p
    def test_known_late_and_early_sound(self):
        with tempfile.TemporaryDirectory() as d:
            for offset in [0,70,-70,240,-240]:
                with self.subTest(offset=offset):
                    result=analyze(self.capture(d,offset),offset)
                    self.assertEqual(len(result['pairs']),6)
                    self.assertLess(result['max_abs_ms'],1)
                    self.assertLessEqual(result['max_abs_interval_ms'],11)
    def test_seek_can_cut_a_pair_only_at_recorded_transition(self):
        with tempfile.TemporaryDirectory() as d:
            p=self.capture(d)
            rows=p.read_text().splitlines()
            for i in range(510,518):
                fields=rows[i].split(',');fields[3]='255';rows[i]=','.join(fields)
            p.write_text('\n'.join(rows))
            with self.assertRaises(AssertionError):analyze(p)
            result=analyze(p,transitions=(51000000,))
            self.assertEqual(len(result['pairs']),6)
    def test_bad_timestamp_cannot_pass(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(ValueError):analyze(self.capture(d,flags=4))
    def test_missing_audio_cannot_pass(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(AssertionError):analyze(self.capture(d,missing=True))

if __name__=='__main__':unittest.main()
