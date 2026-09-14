#!/usr/bin/env python3
"""Plot a 0% cooling log and identify its natural-cooling model."""

import argparse
import json
import math
from pathlib import Path

from analyze_heater_step import read_samples
from analyze_thermal_model import fit_cooling


def write_svg(path: Path, samples, model) -> None:
    width, height, margin = 1000, 560, 70
    start = samples[0][0]
    times = [sample[0] for sample in samples]
    measured_temperatures = [sample[2] for sample in samples]
    modeled_temperatures = [
        model["ambient_temperature_c"]
        + (model["cooling_initial_temperature_c"] - model["ambient_temperature_c"])
        * math.exp(-(time - start) / model["time_constant_s"])
        for time in times
    ]
    xmin, xmax = min(times), max(times)
    ymin = min(measured_temperatures + modeled_temperatures)
    ymax = max(measured_temperatures + modeled_temperatures)

    def point(time: float, temperature: float) -> str:
        x = margin + (time - xmin) * (width - 2 * margin) / (xmax - xmin)
        y = height - margin - (temperature - ymin) * (height - 2 * margin) / (ymax - ymin)
        return f"{x:.1f},{y:.1f}"

    measured = " ".join(point(time, temperature) for time, _, temperature in samples)
    modeled = " ".join(point(time, temperature) for time, temperature in zip(times, modeled_temperatures))
    svg = f"""<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">
<rect width="100%" height="100%" fill="white"/>
<text x="{width / 2}" y="30" text-anchor="middle" font-family="sans-serif" font-size="20">Natural cooling response (0%)</text>
<line x1="{margin}" y1="{height-margin}" x2="{width-margin}" y2="{height-margin}" stroke="black"/>
<line x1="{margin}" y1="{margin}" x2="{margin}" y2="{height-margin}" stroke="black"/>
<polyline points="{measured}" fill="none" stroke="#1976d2" stroke-width="2"/>
<polyline points="{modeled}" fill="none" stroke="#d32f2f" stroke-width="2" stroke-dasharray="8 5"/>
<text x="{width/2}" y="{height-15}" text-anchor="middle" font-family="sans-serif">Time (s)</text>
<text x="18" y="{height/2}" transform="rotate(-90 18 {height/2})" text-anchor="middle" font-family="sans-serif">Temperature (°C)</text>
<text x="{margin+15}" y="{margin+20}" font-family="sans-serif" fill="#1976d2">measured</text>
<text x="{margin+120}" y="{margin+20}" font-family="sans-serif" fill="#d32f2f">cooling model</text>
</svg>
"""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(svg, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--svg", type=Path, required=True)
    parser.add_argument("--json", type=Path, required=True)
    args = parser.parse_args()

    samples = read_samples(args.csv)
    model = fit_cooling(samples)
    model.update({
        "model": "T(t) = Ta + (T0-Ta) exp(-t/tau)",
        "pid_kp": None,
        "pid_ki_per_s": None,
        "pid_kd_s": None,
        "pid_note": "Can them log gia nhiet de tinh process gain va PID.",
    })
    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps(model, indent=2) + "\n", encoding="utf-8")
    write_svg(args.svg, samples, model)
    print(f"Ta={model['ambient_temperature_c']:.3f} C, tau={model['time_constant_s']:.3f} s")
    print("PID chua the tinh chi tu log 0%; can them log gia nhiet 40/70/100%.")
    print(f"Da ghi {args.svg} va {args.json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
