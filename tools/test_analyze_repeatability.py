import csv
import json
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

import analyze_repeatability as analysis


def write_trial(path, *, u=77, duration=600, offset=0, slope=0, status='STOP', step=False):
    with path.open('w', newline='', encoding='utf-8') as stream:
        writer = csv.writer(stream)
        writer.writerow(('elapsed_ms', 'power_percent', 'heater_on', 'temperature_c',
                         'status', 'control_u', 'control_u_max'))
        for t in range(0, duration+1, 10):
            writer.writerow((t*1000, round(u/255*100), 1, 100+offset+slope*t/60,
                             'POWER_CHANGE' if step and t == 300 else 'RUN',
                             u+1 if step and t >= 300 else u, 255))
        writer.writerow(((duration+1)*1000, round(u/255*100), 0, 100+offset+slope*duration/60,
                         status, u+1 if step else u, 255))


class RepeatabilityTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def trial(self, name='trial.csv', **kwargs):
        path = self.root/name
        write_trial(path, **kwargs)
        return path

    def test_reproducible_rising_curves_are_not_steady(self):
        a = analysis.describe(self.trial(slope=.12))
        b = analysis.describe(self.trial('second.csv', slope=.12))
        self.assertAlmostEqual(analysis.compare([a,b])['pairwise_rmse'][0]['rmse_c'], 0)
        self.assertAlmostEqual(a[1]['tail_slope_c_per_min'], .12)
        self.assertFalse(a[1]['tail_criteria_met'])

    def test_constant_offset_is_preserved_and_only_overlap_compared(self):
        a = analysis.describe(self.trial(slope=.1, duration=600))
        b = analysis.describe(self.trial('second.csv', offset=2, slope=.1, duration=900))
        result = analysis.compare([a,b])
        self.assertEqual(result['common_end_s'], 600)
        self.assertEqual(result['compared_time_count'], 61)
        self.assertAlmostEqual(result['pairwise_rmse'][0]['rmse_c'], 2)
        self.assertAlmostEqual(result['mean_between_run_range_c'], 2)
        self.assertAlmostEqual(result['mean_between_run_sample_sd_c'], 2**.5)

    def test_short_window_and_sensor_error_cannot_claim_valid_steady_tail(self):
        _, short = analysis.describe(self.trial(duration=120))
        self.assertFalse(short['tail_criteria_met'])
        _, faulty = analysis.describe(self.trial('faulty.csv', status='SENSOR_ERROR'))
        self.assertTrue(faulty['tail_criteria_met'])
        self.assertFalse(faulty['tail_assessment_valid'])
        self.assertEqual(analysis.tail_label(faulty), 'Cần kiểm tra log')

    def test_incompatible_or_changing_inputs_are_rejected(self):
        a = analysis.describe(self.trial())
        b = analysis.describe(self.trial('second.csv', u=78))
        with self.assertRaisesRegex(ValueError, 'cùng u'):
            analysis.compare([a,b])
        with self.assertRaisesRegex(ValueError, 'u thay đổi'):
            analysis.describe(self.trial('step.csv', step=True))

    def test_jittered_sampling_is_interpolated_without_extrapolation(self):
        path = self.trial()
        other = self.trial('jitter.csv')
        with other.open() as stream:
            rows = list(csv.reader(stream))
        for row in rows[2:-1]:
            time = float(row[0])/1000+.3
            row[0] = str(round(time*1000))
            row[3] = str(100 + time*.01)
        for row in rows[1:2]:
            row[3] = '100'
        with other.open('w',newline='') as stream:
            csv.writer(stream).writerows(rows)
        # Recreate the reference with the same linear response in time.
        write_trial(path, slope=.6)
        result = analysis.compare([analysis.describe(path), analysis.describe(other)])
        self.assertLess(result['pairwise_rmse'][0]['rmse_c'], 1e-10)

    def test_comparison_skips_long_data_gaps(self):
        path = self.trial()
        other = self.trial('gap.csv')
        with other.open() as stream:
            rows = list(csv.reader(stream))
        rows = [rows[0]] + [r for r in rows[1:] if not 200_000 < int(r[0]) < 400_000]
        with other.open('w', newline='') as stream:
            csv.writer(stream).writerows(rows)
        a, b = analysis.describe(path), analysis.describe(other)
        self.assertFalse(b[1]['tail_assessment_valid'])
        result = analysis.compare([a,b])
        self.assertEqual(result['skipped_time_count'], 19)
        self.assertEqual(result['compared_time_count'], 42)

    def test_cli_fixed_trial_exports_graph_without_step_or_model(self):
        path = self.trial(slope=.12)
        self.assertEqual(analysis.main([str(path)]), 0)
        model = json.loads(path.with_name('trial_summary.json').read_text())
        self.assertIsNone(model['comparison'])
        self.assertEqual(model['runs'][0]['control_u'], 77)
        self.assertFalse(model['runs'][0]['tail_criteria_met'])
        svg = ET.parse(path.with_suffix('.svg')).getroot()
        self.assertNotIn('Mô hình ước tính', ''.join(svg.itertext()))
        self.assertIn('CHƯA ĐẠT CỬA SỔ CUỐI', ''.join(svg.itertext()))

    def test_cli_five_runs_exports_comparison_and_summary(self):
        for i in range(5):
            self.trial(f'run{i}.csv', offset=i*.1)
        svg = self.root/'five.svg'
        output = self.root/'five.json'
        summary = self.root/'five_summary.csv'
        result = analysis.main([str(self.root/'run*.csv'), '--svg',str(svg),
                                '--json',str(output), '--summary-csv',str(summary)])
        self.assertEqual(result, 0)
        report = json.loads(output.read_text())
        self.assertEqual(report['run_count'],5)
        self.assertEqual(len(report['comparison']['pairwise_rmse']),10)
        root = ET.parse(svg).getroot()
        self.assertEqual(len(root.findall('{http://www.w3.org/2000/svg}polyline')),5)
        with summary.open(encoding='utf-8-sig') as stream:
            self.assertEqual(len(list(csv.DictReader(stream))),5)

    def test_duplicate_inputs_and_output_overwrite_are_rejected(self):
        path = self.trial()
        original = path.read_bytes()
        with self.assertRaises(SystemExit):
            analysis.main([str(path),str(path)])
        with self.assertRaises(SystemExit):
            analysis.main([str(path),'--summary-csv',str(path)])
        self.assertEqual(path.read_bytes(), original)


if __name__ == '__main__':
    unittest.main()
