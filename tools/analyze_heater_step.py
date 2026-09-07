#!/usr/bin/env python3
"""Estimate a FOPDT heater model and IMC PID settings from firmware CSV logs."""

from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path


def read_samples(path: Path) -> list[tuple[float, float, float]]:
    samples: list[tuple[float, float, float]] = []
    with path.open(newline="", encoding="utf-8-sig") as stream:
        for row in csv.DictReader(line for line in stream if not line.startswith("#")):
            if row.get("status") != "RUN":
                continue
            samples.append(
                (
                    float(row["elapsed_s"]) if "elapsed_s" in row
                    else float(row["elapsed_ms"]) / 1000.0,
                    float(row["power_percent"]) / 100.0,
                    float(row["temperature_c"]),
                )
            )
    if len(samples) < 20:
        raise ValueError("Cần ít nhất 20 mẫu RUN để nhận dạng mô hình")
    return samples


def first_crossing(samples: list[tuple[float, float, float]], target: float) -> float:
    for index in range(1, len(samples)):
        t0, _, y0 = samples[index - 1]
        t1, _, y1 = samples[index]
        if (y0 <= target <= y1) or (y1 <= target <= y0):
            if y1 == y0:
                return t1
            return t0 + (target - y0) * (t1 - t0) / (y1 - y0)
    raise ValueError("Nhiệt độ chưa đạt mức cần thiết; hãy chạy thí nghiệm lâu hơn")


def identify(samples: list[tuple[float, float, float]]) -> dict[str, float]:
    count = len(samples)
    # Firmware emits the first point immediately after applying the step. Keep
    # the baseline short so a long recording does not average in the rise.
    baseline_count = min(3, count)
    tail_count = max(5, count // 5)
    baseline = sum(sample[2] for sample in samples[:baseline_count]) / baseline_count
    steady = sum(sample[2] for sample in samples[-tail_count:]) / tail_count
    power = sum(sample[1] for sample in samples) / count
    delta = steady - baseline
    if power <= 0.0 or delta <= 1.0:
        raise ValueError("Độ tăng nhiệt hoặc công suất không đủ để nhận dạng")

    t_start = samples[0][0]
    dead_time = max(0.0, first_crossing(samples, baseline + 0.02 * delta) - t_start)
    t63 = first_crossing(samples, baseline + 0.632 * delta) - t_start
    tau = t63 - dead_time
    if tau <= 0.0:
        raise ValueError("Không xác định được hằng số thời gian dương")

    gain = delta / power
    tuning_lambda = max(tau, 3.0 * dead_time, 1.0)
    kp = tau / (gain * (tuning_lambda + dead_time))
    ti = tau + dead_time / 2.0
    td = (tau * dead_time) / (2.0 * tau + dead_time) if dead_time > 0.0 else 0.0
    pid_ki = kp / ti
    pid_kd = kp * td
    return {
        "power_step": power,
        "initial_temperature_c": baseline,
        "steady_temperature_c": steady,
        "process_gain_c_per_fraction": gain,
        "dead_time_s": dead_time,
        "time_constant_s": tau,
        "imc_lambda_s": tuning_lambda,
        "pid_kp": kp,
        "pid_ki_per_s": pid_ki,
        "pid_kd_s": pid_kd,
        "firmware_pid_kp_0_to_255": 255.0 * kp,
        "firmware_pid_ki_0_to_255_per_s": 255.0 * pid_ki,
        "firmware_pid_kd_0_to_255_s": 255.0 * pid_kd,
    }


def write_svg(path: Path, samples: list[tuple[float, float, float]], model: dict[str, float]) -> None:
    width, height, margin = 1000, 560, 70
    times = [sample[0] for sample in samples]
    temperatures = [sample[2] for sample in samples]
    xmin, xmax = min(times), max(times)
    ymin, ymax = min(temperatures), max(temperatures)
    if math.isclose(ymin, ymax):
        ymax = ymin + 1.0

    def point(t: float, y: float) -> str:
        x = margin + (t - xmin) * (width - 2 * margin) / (xmax - xmin)
        py = height - margin - (y - ymin) * (height - 2 * margin) / (ymax - ymin)
        return f"{x:.1f},{py:.1f}"

    measured = " ".join(point(t, y) for t, _, y in samples)
    modeled = []
    for t, _, _ in samples:
        shifted = t - xmin - model["dead_time_s"]
        y = model["initial_temperature_c"]
        if shifted > 0.0:
            y += model["process_gain_c_per_fraction"] * model["power_step"] * (
                1.0 - math.exp(-shifted / model["time_constant_s"])
            )
        modeled.append(point(t, y))
    svg = f"""<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">
<rect width="100%" height="100%" fill="white"/>
<text x="{width / 2}" y="30" text-anchor="middle" font-family="sans-serif" font-size="20">Heater step response ({model['power_step'] * 100:.0f}%)</text>
<line x1="{margin}" y1="{height-margin}" x2="{width-margin}" y2="{height-margin}" stroke="black"/>
<line x1="{margin}" y1="{margin}" x2="{margin}" y2="{height-margin}" stroke="black"/>
<polyline points="{measured}" fill="none" stroke="#1976d2" stroke-width="2"/>
<polyline points="{' '.join(modeled)}" fill="none" stroke="#d32f2f" stroke-width="2" stroke-dasharray="8 5"/>
<text x="{width/2}" y="{height-15}" text-anchor="middle" font-family="sans-serif">Time (s)</text>
<text x="18" y="{height/2}" transform="rotate(-90 18 {height/2})" text-anchor="middle" font-family="sans-serif">Temperature (°C)</text>
<text x="{margin+15}" y="{margin+20}" font-family="sans-serif" fill="#1976d2">measured</text>
<text x="{margin+120}" y="{margin+20}" font-family="sans-serif" fill="#d32f2f">FOPDT model</text>
</svg>
"""
    path.write_text(svg, encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path, help="CSV captured from the controller")
    parser.add_argument("--svg", type=Path, default=Path("heater_response.svg"))
    parser.add_argument("--json", type=Path, default=Path("heater_model.json"))
    args = parser.parse_args()
    samples = read_samples(args.csv)
    model = identify(samples)
    args.json.write_text(json.dumps(model, indent=2) + "\n", encoding="utf-8")
    write_svg(args.svg, samples, model)
    print(f"G(s) = {model['process_gain_c_per_fraction']:.6g} exp(-{model['dead_time_s']:.3f}s) / "
          f"({model['time_constant_s']:.3f}s + 1)")
    print(f"PID: Kp={model['pid_kp']:.6g}, Ki={model['pid_ki_per_s']:.6g}/s, "
          f"Kd={model['pid_kd_s']:.6g}s")
    print(f"PID firmware (output 0..255): Kp={model['firmware_pid_kp_0_to_255']:.6g}, "
          f"Ki={model['firmware_pid_ki_0_to_255_per_s']:.6g}/s, "
          f"Kd={model['firmware_pid_kd_0_to_255_s']:.6g}s")
    print(f"Đã ghi {args.svg} và {args.json}")


if __name__ == "__main__":
    main()
