#!/usr/bin/env python3
"""Estimate a FOPDT heater model and IMC PID settings from firmware CSV logs."""

from __future__ import annotations

import argparse
import csv
import json
import math
import sys
from pathlib import Path
from typing import Any


def read_samples(path: Path) -> list[tuple[float, float, float]]:
    samples: list[tuple[float, float, float]] = []
    with path.open(newline="", encoding="utf-8-sig") as stream:
        for row in csv.DictReader(line for line in stream if not line.startswith("#")):
            if row.get("status") != "RUN":
                continue
            samples.append(
                (
                    float(row["elapsed_ms"]) / 1000.0,
                    float(row["power_percent"]) / 100.0,
                    float(row["temperature_c"]),
                )
            )
    if len(samples) < 20:
        raise ValueError("Cần ít nhất 20 mẫu RUN để nhận dạng mô hình")
    return samples


def linear_slope(points: list[tuple[float, float]]) -> float:
    """Return the least-squares slope in output units per second."""
    mean_x = sum(point[0] for point in points) / len(points)
    mean_y = sum(point[1] for point in points) / len(points)
    denominator = sum((point[0] - mean_x) ** 2 for point in points)
    if denominator == 0.0:
        return 0.0
    return sum((x - mean_x) * (y - mean_y) for x, y in points) / denominator


def _fit_at(samples, baseline: float, power: float, dead_time: float, tau: float):
    """Fit gain analytically for fixed dead time and tau, then return SSE."""
    start = samples[0][0]
    features = [
        power * (1.0 - math.exp(-(t - start - dead_time) / tau))
        if t - start > dead_time else 0.0
        for t, _, _ in samples
    ]
    denominator = sum(value * value for value in features)
    if denominator == 0.0:
        return math.inf, 0.0
    gain = sum(
        value * (sample[2] - baseline)
        for value, sample in zip(features, samples)
    ) / denominator
    if gain <= 0.0:
        return math.inf, gain
    sse = sum(
        (sample[2] - (baseline + gain * value)) ** 2
        for value, sample in zip(features, samples)
    )
    return sse, gain


def identify(
    samples: list[tuple[float, float, float]], allow_unsettled_pid: bool = False
) -> dict[str, Any]:
    count = len(samples)
    # Firmware emits the first point immediately after applying the step. Keep
    # the baseline short so a long recording does not average in the rise.
    baseline_count = min(3, count)
    baseline = sum(sample[2] for sample in samples[:baseline_count]) / baseline_count
    power = sum(sample[1] for sample in samples) / count
    duration = samples[-1][0] - samples[0][0]
    delta = samples[-1][2] - baseline
    if power <= 0.0 or delta <= 1.0:
        raise ValueError("Độ tăng nhiệt hoặc công suất không đủ để nhận dạng")

    # Fit every measured point instead of treating the moving tail as a steady
    # state. Gain is solved analytically; L and T use a deterministic log grid.
    sample_period = duration / (count - 1)
    tau_min, tau_max = max(sample_period, duration / 100.0), duration * 20.0
    dead_max = min(duration / 4.0, 600.0)
    best = (math.inf, 0.0, duration, 0.0)
    for dead_index in range(81):
        candidate_dead = dead_max * dead_index / 80.0
        for tau_index in range(121):
            ratio = tau_index / 120.0
            candidate_tau = tau_min * (tau_max / tau_min) ** ratio
            sse, candidate_gain = _fit_at(
                samples, baseline, power, candidate_dead, candidate_tau
            )
            if sse < best[0]:
                best = (sse, candidate_dead, candidate_tau, candidate_gain)
    sse, dead_time, tau, gain = best

    mean_temperature = sum(sample[2] for sample in samples) / count
    total_squares = sum((sample[2] - mean_temperature) ** 2 for sample in samples)
    r_squared = 1.0 - sse / total_squares if total_squares > 0.0 else 0.0
    rmse = math.sqrt(sse / count)
    tail_count = max(10, count // 5)
    tail_slope = 60.0 * linear_slope([(t, y) for t, _, y in samples[-tail_count:]])
    steady_threshold = max(0.05, 0.001 * abs(delta))
    steady_reached = (
        abs(tail_slope) <= steady_threshold
        and duration >= dead_time + 3.0 * tau
    )
    fit_at_tau_limit = tau >= 0.95 * tau_max
    model_valid = steady_reached and r_squared >= 0.90 and not fit_at_tau_limit
    warnings = []
    if not steady_reached:
        warnings.append(
            f"Nhiệt độ cuối vẫn đổi {tail_slope:.3f} °C/phút hoặc phép đo chưa đủ 3T; "
            "K và T còn là ngoại suy."
        )
    if fit_at_tau_limit:
        warnings.append(
            "T chạm giới hạn tìm kiếm; dữ liệu gần tuyến tính và mô hình "
            "không định danh được."
        )
    if r_squared < 0.90:
        warnings.append(f"FOPDT khớp kém (R²={r_squared:.4f}).")

    steady = baseline + gain * power
    tuning_lambda = max(tau, 3.0 * dead_time, 1.0)
    kp = tau / (gain * (tuning_lambda + dead_time))
    ti = tau + dead_time / 2.0
    td = (tau * dead_time) / (2.0 * tau + dead_time) if dead_time > 0.0 else 0.0
    pid_ki = kp / ti
    pid_kd = kp * td
    result = {
        "identification_method": "full_curve_least_squares_fopdt",
        "power_step": power,
        "initial_temperature_c": baseline,
        "steady_temperature_c": steady,
        "process_gain_c_per_fraction": gain,
        "dead_time_s": dead_time,
        "time_constant_s": tau,
        "imc_lambda_s": tuning_lambda,
        "fit_rmse_c": rmse,
        "fit_r_squared": r_squared,
        "tail_slope_c_per_min": tail_slope,
        "steady_state_reached": steady_reached,
        "model_valid_for_pid": model_valid,
        "warnings": warnings,
    }
    publish_pid = model_valid or allow_unsettled_pid
    result.update(
        {
            "pid_kp": kp if publish_pid else None,
            "pid_ki_per_s": pid_ki if publish_pid else None,
            "pid_kd_s": pid_kd if publish_pid else None,
            "firmware_pid_kp_0_to_255": 255.0 * kp if publish_pid else None,
            "firmware_pid_ki_0_to_255_per_s": 255.0 * pid_ki if publish_pid else None,
            "firmware_pid_kd_0_to_255_s": 255.0 * pid_kd if publish_pid else None,
        }
    )
    return result


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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path, help="CSV captured from the controller")
    parser.add_argument("--svg", type=Path, default=Path("heater_response.svg"))
    parser.add_argument("--json", type=Path, default=Path("heater_model.json"))
    parser.add_argument(
        "--allow-unsettled-pid", action="store_true",
        help="vẫn xuất PID ngoại suy khi dữ liệu chưa đạt xác lập (không khuyến nghị)",
    )
    args = parser.parse_args()
    samples = read_samples(args.csv)
    model = identify(samples, args.allow_unsettled_pid)
    args.json.write_text(json.dumps(model, indent=2) + "\n", encoding="utf-8")
    write_svg(args.svg, samples, model)
    print(f"G(s) = {model['process_gain_c_per_fraction']:.6g} exp(-{model['dead_time_s']:.3f}s) / "
          f"({model['time_constant_s']:.3f}s + 1)")
    print(f"Độ khớp: RMSE={model['fit_rmse_c']:.3f} °C, R²={model['fit_r_squared']:.5f}; "
          f"độ dốc cuối={model['tail_slope_c_per_min']:.3f} °C/phút")
    for warning in model["warnings"]:
        print(f"CẢNH BÁO: {warning}", file=sys.stderr)
    if model["pid_kp"] is None:
        print("PID: KHÔNG XUẤT vì dữ liệu chưa đủ tin cậy. Hãy đo lâu hơn hoặc dùng "
              "--allow-unsettled-pid để xem giá trị ngoại suy.")
    else:
        print(f"PID: Kp={model['pid_kp']:.6g}, Ki={model['pid_ki_per_s']:.6g}/s, "
              f"Kd={model['pid_kd_s']:.6g}s")
        print(f"PID firmware (output 0..255): Kp={model['firmware_pid_kp_0_to_255']:.6g}, "
              f"Ki={model['firmware_pid_ki_0_to_255_per_s']:.6g}/s, "
              f"Kd={model['firmware_pid_kd_0_to_255_s']:.6g}s")
    print(f"Đã ghi {args.svg} và {args.json}")
    # Batch files use ERRORLEVEL to decide whether they may announce that the
    # captured data are suitable for initial PID tuning. Keep the SVG and JSON
    # diagnostics, but return a distinct failure status when PID was withheld.
    return 0 if model["pid_kp"] is not None else 2


if __name__ == "__main__":
    raise SystemExit(main())
