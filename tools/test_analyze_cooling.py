import math
import tempfile
import unittest
from pathlib import Path

import analyze_cooling


class AnalyzeCoolingTest(unittest.TestCase):
    def test_writes_cooling_graph_and_model_without_fake_pid(self):
        samples = [
            (float(t), 0.0, 25.0 + 100.0 * math.exp(-t / 600.0))
            for t in range(0, 3601, 10)
        ]
        model = analyze_cooling.fit_cooling(samples)
        model.update({"pid_kp": None})
        with tempfile.TemporaryDirectory() as directory:
            svg = Path(directory) / "cooling.svg"
            analyze_cooling.write_svg(svg, samples, model)
            self.assertIn("Natural cooling response", svg.read_text(encoding="utf-8"))
            self.assertIsNone(model["pid_kp"])


if __name__ == "__main__":
    unittest.main()
