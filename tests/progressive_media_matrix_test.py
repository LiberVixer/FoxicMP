#!/usr/bin/env python3
"""Guard against gaps and false acceptance in the finite media plan."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('media_matrix', ROOT/'tools/verify_progressive_media_matrix.py')
matrix_tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(matrix_tool)


class MediaMatrixTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = json.loads(matrix_tool.MATRIX.read_text())
        cls.contracts = matrix_tool.source_contracts()

    def setUp(self):
        self.data = copy.deepcopy(self.source)

    def verify(self):
        return matrix_tool.verify(self.data, self.contracts)

    def test_current_source_inventory(self):
        result = self.verify()
        self.assertEqual(result['boundedExisting'], result['native']+result['continuous'])
        self.assertFalse(result['newPlaybackAccepted'])
        self.assertFalse(result['playersStarted'])

    def test_duplicate_profile_rejected(self):
        self.data['existingProfiles'].append(copy.deepcopy(self.data['existingProfiles'][0]))
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            self.verify()

    def test_missing_advertised_profile_rejected(self):
        self.data['existingProfiles'].pop(0)
        with self.assertRaisesRegex(ValueError, 'Missing/extra'):
            self.verify()

    def test_declared_constraint_drift_rejected(self):
        self.data['contractDigests']['continuous'] = '0'*64
        with self.assertRaisesRegex(ValueError, 'admission drift'):
            self.verify()

    def test_remux_constraints_cannot_be_widened(self):
        row = next(r for r in self.data['existingProfiles'] if r['path'] == 'progressive-remux')
        row['admission']['audio'] = True
        with self.assertRaisesRegex(ValueError, 'Altered remux admission'):
            self.verify()

    def test_planned_row_is_not_pass(self):
        self.data['remainingFamilies'][0]['status'] = 'PASS'
        with self.assertRaisesRegex(ValueError, 'not a playback PASS'):
            self.verify()

    def test_bounded_acceptance_requires_an_evidence_file(self):
        row=self.data['remainingFamilies'][0];row['status']='accepted-bounded';row.pop('acceptanceEvidence',None)
        with self.assertRaisesRegex(ValueError,'Missing family acceptance evidence'):
            self.verify()

    def test_m4a_evidence_cannot_close_other_mp4_codecs(self):
        row=next(r for r in self.data['remainingFamilies'] if r['id']=='M02')
        row.update(status='accepted-bounded',acceptanceEvidence='docs/ProgressiveM4AValidation-2026-11-18.json',boundedScope='all MP4')
        with self.assertRaisesRegex(ValueError,'Family evidence is not a PASS'):
            self.verify()

    def test_linux_alone_cannot_cover_windows(self):
        self.data['remainingFamilies'][0]['editions'] = ['linux-qt']
        with self.assertRaisesRegex(ValueError, 'missing an edition'):
            self.verify()

    def test_unknown_work_id_rejected(self):
        self.data['remainingFamilies'][0]['owners'] = ['P02.absent']
        with self.assertRaisesRegex(ValueError, 'terminal work ID'):
            self.verify()

    def test_missing_spec_area_rejected(self):
        del self.data['specCoverage']['Графические субтитры']
        with self.assertRaisesRegex(ValueError, 'Missing media-spec area'):
            self.verify()

    def test_no_ready_before_complete_shortcut(self):
        self.data['remainingFamilies'][0]['gates'].remove('incomplete-playback')
        with self.assertRaisesRegex(ValueError, 'acceptance gate'):
            self.verify()

    def test_unknown_fixture_cannot_hide_negative_class(self):
        self.data['negativeCases'].pop()
        with self.assertRaisesRegex(ValueError, 'negative outcome'):
            self.verify()

    def test_invalid_ac3_channels_rejected(self):
        self.data['variantRules']['channelValidity']['ac3'].append('7.1')
        with self.assertRaisesRegex(ValueError, 'Invalid AC-3'):
            self.verify()

    def test_input_family_cannot_disappear(self):
        self.data['remainingFamilies'].pop()
        with self.assertRaisesRegex(ValueError, 'input-family scope changed'):
            self.verify()

    def test_security_axis_cannot_disappear(self):
        self.data['scenarioAxes'] = [r for r in self.data['scenarioAxes'] if r['id'] != 'X05']
        with self.assertRaisesRegex(ValueError, 'cross-cutting scope changed'):
            self.verify()

    def test_mpeg_rates_cannot_be_cross_multiplied(self):
        self.data['variantRules']['mp3Rates']['mpeg1'].append(8000)
        with self.assertRaisesRegex(ValueError, 'Cartesian product'):
            self.verify()


if __name__ == '__main__':
    unittest.main()
