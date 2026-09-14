#!/usr/bin/env python3
"""Identify one thermal energy-balance model from heating and cooling logs."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any

from analyze_heater_step import read_samples


def _linear_fit(x: list[float], y: list[float]) -> tuple[float, float, float]:
    """Return intercept, slope and SSE for y = intercept + slope*x."""
    mean_x = sum(x) / len(x)
    mean_y = sum(y) / len(y)
    denominator = sum((value - mean_x) ** 2 for value in x)
    if denominator == 0.0:
        return mean_y, 0.0, math.inf
    slope = sum((a - mean_x) * (b - mean_y) for a, b in zip(x, y)) / denominator
    intercept = mean_y - slope * mean_x
    sse = sum((b - intercept - slope * a) ** 2 for a, b in zip(x, y))
    return intercept, slope, sse


def fit_cooling(samples: list[tuple[float, float, float]]) -> dict[str, float]:
    """Fit T(t) = Ta + (T0-Ta)*exp(-t/tau) without requiring known ambient."""
    if any(power != 0.0 for _, power, _ in samples):
        raise ValueError("Log nguội phải có power_percent = 0 cho mọi mẫu RUN")
    start = samples[0][0]
    duration = samples[-1][0] - start
    drop = samples[0][2] - samples[-1][2]
    if duration <= 0.0 or drop < 2.0:
        raise ValueError("Dữ liệu nguội phải giảm ít nhất 2 °C trong một khoảng thời gian dương")

    # For a fixed tau, ambient and amplitude are a two-parameter linear fit.
    period = duration / (len(samples) - 1)
    tau_min = max(period, duration / 100.0)
    tau_max = duration * 20.0
    best = (math.inf, 0.0, 0.0, 0.0)
    temperatures = [temperature for _, _, temperature in samples]
    for index in range(401):
        tau = tau_min * (tau_max / tau_min) ** (index / 400.0)
        decay = [math.exp(-(time - start) / tau) for time, _, _ in samples]
        ambient, amplitude, sse = _linear_fit(decay, temperatures)
        if amplitude > 0.0 and sse < best[0]:
            best = (sse, tau, ambient, amplitude)
    sse, tau, ambient, amplitude = best
    mean = sum(temperatures) / len(temperatures)
    total = sum((value - mean) ** 2 for value in temperatures)
    return {
        "ambient_temperature_c": ambient,
        "time_constant_s": tau,
        "cooling_initial_temperature_c": ambient + amplitude,
        "cooling_rmse_c": math.sqrt(sse / len(samples)),
        "cooling_r_squared": 1.0 - sse / total if total else 0.0,
    }


def fit_gain(
    runs: list[list[tuple[float, float, float]]], ambient: float, tau: float
) -> tuple[float, float]:
    estimates: list[tuple[float, float]] = []
    for samples in runs:
        for first, second in zip(samples, samples[1:]):
            dt = second[0] - first[0]
            power = (first[1] + second[1]) / 2.0
            if dt <= 0.0 or power <= 0.0:
                continue
            midpoint = (first[2] + second[2]) / 2.0
            target = tau * (second[2] - first[2]) / dt + midpoint - ambient
            estimates.append((power, target))
    denominator = sum(power * power for power, _ in estimates)
    if denominator == 0.0:
        raise ValueError("Không có đoạn gia nhiệt với công suất lớn hơn 0%")
    gain = sum(power * target for power, target in estimates) / denominator
    rmse = math.sqrt(
        sum((target - gain * power) ** 2 for power, target in estimates)
        / len(estimates)
    )
    if gain <= 0.0:
        raise ValueError("Hệ số khuếch đại nhiệt ước lượng không dương")
    return gain, rmse


def identify(
    cooling, heating_runs, tuning_lambda_s: float | None = None
) -> dict[str, Any]:
    result: dict[str, Any] = fit_cooling(cooling)
    gain, equation_rmse = fit_gain(
        heating_runs, result["ambient_temperature_c"], result["time_constant_s"]
    )
    tuning_lambda = result["time_constant_s"] if tuning_lambda_s is None else tuning_lambda_s
    if tuning_lambda <= 0.0:
        raise ValueError("Hằng số IMC lambda phải lớn hơn 0")
    # Conservative IMC PI for K/(tau*s+1), with derivative explicitly zero.
    pi_kp = result["time_constant_s"] / (gain * tuning_lambda)
    pi_ki = pi_kp / result["time_constant_s"]
    result.update({
        "model": "dT/dt = -(T-Ta)/tau + (K/tau)u",
        "process_gain_c_per_fraction": gain,
        "heating_equation_rmse_c": equation_rmse,
        "heating_run_count": len(heating_runs),
        "imc_lambda_s": tuning_lambda,
        "pi_kp": pi_kp,
        "pi_ki_per_s": pi_ki,
        "pi_kd_s": 0.0,
        "firmware_pi_kp_0_to_255": 255.0 * pi_kp,
        "firmware_pi_ki_0_to_255_per_s": 255.0 * pi_ki,
    })
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("heating", nargs="+", type=Path, help="các CSV gia nhiệt 40/70/100%%")
    parser.add_argument("--cooling", required=True, type=Path, help="CSV nguội tự nhiên 0%%")
    parser.add_argument("--json", type=Path, default=Path("thermal_model.json"))
    parser.add_argument("--lambda-s", type=float, help="IMC lambda (mặc định bằng tau, bảo thủ)")
    args = parser.parse_args()

    cooling = read_samples(args.cooling)
    heating = [read_samples(path) for path in args.heating]
    model = identify(cooling, heating, args.lambda_s)
    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps(model, indent=2) + "\n", encoding="utf-8")
    print(f"Ta={model['ambient_temperature_c']:.3f} °C, tau={model['time_constant_s']:.3f} s, "
          f"K={model['process_gain_c_per_fraction']:.3f} °C/fraction")
    print(f"Cooling: R²={model['cooling_r_squared']:.5f}, RMSE={model['cooling_rmse_c']:.3f} °C")
    print(f"PI ban đầu: Kp={model['pi_kp']:.6g}, Ki={model['pi_ki_per_s']:.6g}/s, Kd=0")
    print(f"Đã ghi {args.json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
