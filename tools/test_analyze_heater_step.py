import math
import unittest

import analyze_heater_step


def fopdt_samples(gain=40.0, dead_time=30.0, tau=180.0, duration=1200.0):
    samples = []
    for t in range(0, int(duration) + 1, 10):
        rise = (
            0.0
            if t <= dead_time
            else gain * 0.5 * (1.0 - math.exp(-(t - dead_time) / tau))
        )
        samples.append((float(t), 0.5, 25.0 + rise))
    return samples


class AnalyzeHeaterStepTest(unittest.TestCase):
    def test_full_curve_fit_recovers_settled_fopdt_and_publishes_pid(self):
        model = analyze_heater_step.identify(fopdt_samples())

        self.assertAlmostEqual(model["process_gain_c_per_fraction"], 40.0, delta=1.0)
        self.assertAlmostEqual(model["dead_time_s"], 30.0, delta=8.0)
        self.assertAlmostEqual(model["time_constant_s"], 180.0, delta=15.0)
        self.assertGreater(model["fit_r_squared"], 0.999)
        self.assertTrue(model["steady_state_reached"])
        self.assertTrue(model["model_valid_for_pid"])
        self.assertIsNotNone(model["pid_kp"])

    def test_rising_tail_is_flagged_and_pid_is_suppressed(self):
        samples = [(float(t), 0.4, 33.2 + 0.02 * t) for t in range(0, 2101, 10)]

        model = analyze_heater_step.identify(samples)

        self.assertGreater(model["tail_slope_c_per_min"], 1.0)
        self.assertFalse(model["steady_state_reached"])
        self.assertFalse(model["model_valid_for_pid"])
        self.assertIsNone(model["pid_kp"])
        self.assertTrue(model["warnings"])

    def test_unsettled_pid_requires_explicit_override(self):
        samples = [(float(t), 0.4, 30.0 + 0.01 * t) for t in range(0, 1001, 10)]

        model = analyze_heater_step.identify(samples, allow_unsettled_pid=True)

        self.assertFalse(model["model_valid_for_pid"])
        self.assertIsNotNone(model["pid_kp"])


if __name__ == "__main__":
    unittest.main()
