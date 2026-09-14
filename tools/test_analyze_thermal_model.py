import math
import unittest

import analyze_thermal_model


def run(power, initial, ambient=25.0, gain=80.0, tau=600.0, duration=3600):
    steady = ambient + gain * power
    return [
        (float(t), power, steady + (initial - steady) * math.exp(-t / tau))
        for t in range(0, duration + 1, 10)
    ]


class AnalyzeThermalModelTest(unittest.TestCase):
    def test_combines_cooling_and_multiple_heating_runs(self):
        cooling = run(0.0, 125.0)
        heating = [run(0.4, 25.0), run(0.7, 25.0), run(1.0, 25.0)]

        model = analyze_thermal_model.identify(cooling, heating)

        self.assertAlmostEqual(model["ambient_temperature_c"], 25.0, delta=0.2)
        self.assertAlmostEqual(model["time_constant_s"], 600.0, delta=10.0)
        self.assertAlmostEqual(model["process_gain_c_per_fraction"], 80.0, delta=0.5)
        self.assertEqual(model["heating_run_count"], 3)
        self.assertGreater(model["cooling_r_squared"], 0.999)
        self.assertAlmostEqual(model["pi_kp"], 1.0 / 80.0, places=5)
        self.assertAlmostEqual(model["pi_ki_per_s"], 1.0 / (80.0 * 600.0), places=7)
        self.assertEqual(model["pi_kd_s"], 0.0)

    def test_rejects_nonzero_power_in_cooling_log(self):
        with self.assertRaisesRegex(ValueError, "power_percent = 0"):
            analyze_thermal_model.fit_cooling(run(0.1, 125.0))


if __name__ == "__main__":
    unittest.main()
