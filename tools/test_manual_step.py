"""Regression checks for nonzero-baseline experiments and exact-u log decoding."""
import csv
import math
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import analyze_heater_step as analysis
import capture_ram_log as capture
import extract_ram_log as extract


def manual_samples():
    samples = analysis.SampleSeries()
    samples.manual = True
    samples.step_events = (600.0,)
    for t in range(0, 2401, 2):
        temp = 80.0
        if t > 630:
            temp += 20.0 * (1.0 - math.exp(-(t - 630) / 180.0))
        samples.append((float(t), 0.2 if t < 600 else 0.4, temp))
    return samples


class ManualStepTest(unittest.TestCase):
    def test_nonzero_u1_uses_delta_u_and_measured_baseline(self):
        model = analysis.identify(manual_samples())
        self.assertAlmostEqual(model['power_step'], 0.2)
        self.assertAlmostEqual(model['u1'], 51)
        self.assertAlmostEqual(model['u2'], 102)
        self.assertAlmostEqual(model['initial_temperature_c'], 80)
        self.assertAlmostEqual(model['process_gain_c_per_fraction'], 100, delta=2)
        self.assertAlmostEqual(model['process_gain_c_per_u'], 100 / 255, delta=.01)
        self.assertAlmostEqual(model['measured_process_gain_c_per_u'], 100 / 255, delta=.001)
        self.assertAlmostEqual(model['dead_time_s'], 30, delta=8)
        self.assertAlmostEqual(model['time_constant_s'], 180, delta=15)
        self.assertTrue(model['initial_steady_state_reached'])
        self.assertTrue(model['model_valid_for_pid'])

    def test_unsettled_t1_blocks_pid_despite_settled_t2(self):
        samples = manual_samples()
        for i, (t, power, temp) in enumerate(samples):
            if t < 600:
                samples[i] = (t, power, temp + .003 * (t - 600))
        model = analysis.identify(samples)
        self.assertFalse(model['initial_steady_state_reached'])
        self.assertFalse(model['model_valid_for_pid'])
        self.assertIsNone(model['pid_kp'])

    def test_large_oscillations_block_steady_state_even_with_small_drift(self):
        samples = manual_samples()
        for i, (t, power, temp) in enumerate(samples):
            if t > 1800:
                samples[i] = (t, power, temp + 1.5 * math.sin(2 * math.pi * t / 10))
        model = analysis.identify(samples)
        self.assertGreater(model['final_band_c'], 2)
        self.assertFalse(model['steady_state_reached'])
        self.assertIsNone(model['pid_kp'])

    def test_multiple_steps_are_rejected(self):
        samples = manual_samples()
        samples[-1] = (2400.0, .5, 100.0)
        with self.assertRaisesRegex(ValueError, 'đúng một bước'):
            analysis.identify(samples)

    def test_v2_csv_keeps_exact_u_and_step_timestamp(self):
        payload = extract.HEADER.pack(extract.MAGIC, 2, 12, 7204, 3, 1)
        payload += extract.SAMPLE_V2.pack(0, 801, 20, 1, 0, 51, 255, 0)
        payload += extract.SAMPLE_V2.pack(600003, 802, 24, 1, 7, 61, 255, 0)
        payload += extract.SAMPLE_V2.pack(601100, 803, 24, 0, 0, 61, 255, 0)
        rows, complete, _, capacity = extract.decode(payload)
        self.assertTrue(complete)
        self.assertEqual(capacity, 7204)
        self.assertEqual(rows[1], (600003, 24, 1, 80.2, 'POWER_CHANGE', 61, 255))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'manual.csv'
            capture.write_csv(path, rows)
            samples = analysis.read_samples(path)
        self.assertAlmostEqual(samples[1][1], 61 / 255)
        self.assertEqual(samples.step_events, (600.003,))

    def test_v2_rejects_invalid_control_scale(self):
        payload = extract.HEADER.pack(extract.MAGIC, 2, 12, 7204, 1, 1)
        payload += extract.SAMPLE_V2.pack(0, 800, 20, 1, 0, 1, 0, 0)
        with self.assertRaisesRegex(ValueError, 'thang u'):
            extract.decode(payload)

    def test_no_step_still_exports_actual_graph_and_diagnostic_json(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = root / 'manual.csv'
            capture.write_csv(path, [(t * 1000, 20, 1, 80., 'RUN', 51, 255)
                                     for t in range(25)])
            argv = ['analysis', str(path), '--svg', str(root / 'out' / 'plot.svg'),
                    '--json', str(root / 'out' / 'model.json')]
            with mock.patch('sys.argv', argv):
                self.assertEqual(analysis.main(), 2)
            self.assertIn('actual PT100 readings', (root / 'out' / 'plot.svg').read_text())
            self.assertIn('đúng một bước', (root / 'out' / 'model.json').read_text())

    def test_cli_complete_experiment_and_sensor_error(self):
        import json
        import xml.etree.ElementTree as ET
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            rows = [(round(t * 1000), round(power * 100), 1, temp,
                     'POWER_CHANGE' if t == 600 else 'RUN', round(power * 255), 255)
                    for t, power, temp in manual_samples()]
            path = root / 'manual.csv'
            argv = ['analysis', str(path), '--svg', str(root / 'plot.svg'),
                    '--json', str(root / 'model.json')]
            for status, expected in [('STOP', 0), ('SENSOR_ERROR', 2), ('RUN', 2)]:
                capture.write_csv(path, rows + [(2401000, 40, 0, 100., status, 102, 255)])
                with mock.patch('sys.argv', argv):
                    self.assertEqual(analysis.main(), expected)
                model = json.loads((root / 'model.json').read_text())
                self.assertEqual(model['model_valid_for_pid'], status == 'STOP')
                if status != 'STOP':
                    self.assertIsNone(model['pid_kp'])
                svg = ET.parse(root / 'plot.svg').getroot()
                ns = {'s': 'http://www.w3.org/2000/svg'}
                markers = {node.get('data-marker'): float(node.get('data-time-s'))
                           for node in svg.findall('s:line[@data-marker]', ns)}
                self.assertEqual(markers['Đổi u'], 600.0)
                self.assertAlmostEqual(markers['Hết trễ L'], 600 + model['dead_time_s'], places=5)
                self.assertAlmostEqual(markers['63,2% mô hình'],
                                       600 + model['dead_time_s'] + model['time_constant_s'], places=5)
                points = svg.findall('s:circle[@class="hover-point"]', ns)
                self.assertEqual(len(points), len(rows) + (status == 'RUN'))
                self.assertEqual(float(points[0].get('data-temperature-c')), 80.0)
                texts = ' '.join(svg.itertext())
                self.assertIn('MIN / MAX ĐO ĐƯỢC', texts)
                self.assertIn('phút:giây', texts)


if __name__ == '__main__':
    unittest.main()
