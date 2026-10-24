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

from thermal_chart import time_label, write_thermal_svg


class SampleSeries(list):
    """Measurements plus acquisition metadata (legacy CSV remains supported)."""
    manual = False
    control_max = 255
    step_events = ()
    final_status = None


def read_samples(path: Path) -> list[tuple[float, float, float]]:
    samples = SampleSeries()
    step_events = []
    control_max = None
    with path.open(newline="", encoding="utf-8-sig") as stream:
        for row in csv.DictReader(line for line in stream if not line.startswith("#")):
            samples.final_status = row.get("status")
            if row.get("control_u_max"):
                samples.manual = True
                maximum = int(row["control_u_max"])
                u = int(row["control_u"])
                if maximum <= 0 or not 0 <= u <= maximum or (
                    control_max is not None and maximum != control_max
                ):
                    raise ValueError("Thang u không hợp lệ hoặc thay đổi giữa phép đo")
                control_max = samples.control_max = maximum
            if row.get("status") not in ("RUN", "POWER_CHANGE"):
                continue
            if row.get("status") == "POWER_CHANGE":
                step_events.append(float(row["elapsed_ms"]) / 1000.0)
            samples.append(
                (
                    float(row["elapsed_ms"]) / 1000.0,
                    (float(row["control_u"]) / control_max if samples.manual
                     else float(row["power_percent"]) / 100.0),
                    float(row["temperature_c"]),
                )
            )
    if len(samples) < 2:
        raise ValueError("Cần ít nhất hai mẫu nhiệt độ thực")
    if any(not all(math.isfinite(v) for v in point) for point in samples):
        raise ValueError("Dữ liệu chứa NaN hoặc vô cực")
    if any(b[0] < a[0] for a, b in zip(samples, samples[1:])):
        raise ValueError("Thời gian lấy mẫu không tăng")
    samples.step_events = tuple(step_events)
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
    samples: list[tuple[float, float, float]], allow_unsettled_pid: bool = False,
    *, steady_window_s: float = 300.0, steady_band_c: float = 1.0,
    steady_slope_c_per_min: float = 0.05,
) -> dict[str, Any]:
    if min(steady_window_s, steady_band_c, steady_slope_c_per_min) <= 0:
        raise ValueError("Ngưỡng xác lập phải lớn hơn 0")
    if len(samples) < 20:
        raise ValueError("Cần ít nhất 20 mẫu để nhận dạng mô hình")
    control_max = getattr(samples, "control_max", 255)
    changes = [i for i in range(1, len(samples))
               if not math.isclose(samples[i][1], samples[i - 1][1], abs_tol=1e-9)]
    manual = getattr(samples, "manual", False) or bool(changes)
    initial_steady = True
    initial_slope = initial_band = None
    if manual:
        if len(changes) != 1:
            raise ValueError("Phép đo phải có đúng một bước u1 → u2. "
                             "Hãy tách lần dò công suất và chạy lại phép đo chính thức.")
        index = changes[0]
        step_time = samples[index][0]
        events = getattr(samples, "step_events", ())
        if getattr(samples, "manual", False) and events != (step_time,):
            raise ValueError("Thiếu hoặc sai dấu mốc POWER_CHANGE của bước u1 → u2")
        before = [p for p in samples[:index] if p[0] >= step_time - steady_window_s]
        # A sample can fall just before the window boundary due to sensor jitter.
        preceding = samples[max(0, index - len(before) - 1)]
        if before and preceding[0] < before[0][0]:
            before.insert(0, preceding)
        initial_power, final_power = samples[0][1], samples[index][1]
        if final_power <= initial_power:
            raise ValueError("Cần bước tăng u2 > u1 cho phép đo gia nhiệt này")
        if len(before) < 20:
            raise ValueError("Cần ghi đủ đoạn T1 ổn định trước khi đổi u")
        baseline = sum(p[2] for p in before) / len(before)
        initial_slope = 60.0 * linear_slope([(p[0], p[2]) for p in before])
        initial_band = max(p[2] for p in before) - min(p[2] for p in before)
        initial_steady = (step_time - before[0][0] >= steady_window_s
                          and abs(initial_slope) <= steady_slope_c_per_min
                          and initial_band <= steady_band_c)
        power = final_power - initial_power
        samples = samples[index:]
    else:
        step_time = samples[0][0]
        initial_power, final_power = 0.0, samples[0][1]
    count = len(samples)
    if count < 20 or samples[-1][0] <= step_time:
        raise ValueError("Cần ít nhất 20 mẫu sau bước u2")
    # Firmware emits the first point immediately after applying the step. Keep
    # the baseline short so a long recording does not average in the rise.
    baseline_count = min(3, count)
    if not manual:
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
    # Bound grid cost for long 1 Hz records; evaluate diagnostics on every sample.
    stride = max(1, (count + 599) // 600)
    fit_samples = samples[::stride]
    if fit_samples[-1] != samples[-1]:
        fit_samples = fit_samples + [samples[-1]]
    for dead_index in range(81):
        candidate_dead = dead_max * dead_index / 80.0
        for tau_index in range(121):
            ratio = tau_index / 120.0
            candidate_tau = tau_min * (tau_max / tau_min) ** ratio
            sse, candidate_gain = _fit_at(
                fit_samples, baseline, power, candidate_dead, candidate_tau
            )
            if sse < best[0]:
                best = (sse, candidate_dead, candidate_tau, candidate_gain)
    _, dead_time, tau, gain = best
    sse, gain = _fit_at(samples, baseline, power, dead_time, tau)

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
    tail = [p for p in samples if p[0] >= samples[-1][0] - steady_window_s]
    final_band = max(p[2] for p in tail) - min(p[2] for p in tail)
    measured_final = sum(p[2] for p in tail) / len(tail)
    if manual:
        tail_slope = 60.0 * linear_slope([(p[0], p[2]) for p in tail])
        steady_reached = (duration >= steady_window_s
                          and abs(tail_slope) <= steady_slope_c_per_min
                          and final_band <= steady_band_c
                          and duration >= dead_time + 3.0 * tau)
    model_valid = initial_steady and steady_reached and r_squared >= 0.90 and not fit_at_tau_limit
    warnings = []
    if not initial_steady:
        warnings.append("Đoạn T1 chưa đủ thời gian hoặc còn dao động/trôi vượt ngưỡng.")
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
        "step_time_s": step_time,
        "initial_power_fraction": initial_power,
        "final_power_fraction": final_power,
        "control_u_max": control_max,
        "u1": initial_power * control_max,
        "u2": final_power * control_max,
        "initial_steady_state_reached": initial_steady,
        "initial_slope_c_per_min": initial_slope,
        "initial_band_c": initial_band,
        "final_band_c": final_band,
        "measured_final_temperature_c": measured_final,
        "measured_process_gain_c_per_u": ((measured_final - baseline) / (power * control_max)
                                           if manual and initial_steady and steady_reached else None),
        "process_gain_c_per_u": gain / control_max,
        "steady_window_s": steady_window_s,
        "steady_band_c": steady_band_c,
        "steady_slope_c_per_min": steady_slope_c_per_min,
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


def write_svg(path: Path, samples, model=None) -> None:
    model = model or {}
    control_max = model.get("control_u_max", getattr(samples, "control_max", 255))
    manual = getattr(samples, "manual", False) or any(
        not math.isclose(a[1], b[1], abs_tol=1e-9) for a, b in zip(samples, samples[1:])
    )
    fitted = "time_constant_s" in model
    modeled, references, parameters, markers = [], [], [], []
    if manual:
        parameters.append(("u đầu → u cuối (số đo)",
                           f"{samples[0][1]*control_max:.0f} → {samples[-1][1]*control_max:.0f}"))
    else:
        parameters.append(("Công suất trong lần đo",
                           f"{samples[0][1]*100:.1f}% · u = {samples[0][1]*control_max:.1f}"))
    if fitted:
        step = model["step_time_s"]
        initial = model["initial_temperature_c"]
        final = model.get("measured_final_temperature_c", samples[-1][2])
        initial_ok = model.get("initial_steady_state_reached", False)
        final_ok = model.get("steady_state_reached", False)
        parameters += [
            ("T1 — TB trước bước" if manual else "Nhiệt độ đầu mô hình",
             f"{initial:.2f} °C" + (" · chưa xác lập" if manual and not initial_ok else "")),
            ("T2 — TB cuối đoạn đo", f"{final:.2f} °C · " + ("đã xác lập" if final_ok else "chưa xác lập")),
            ("Thời điểm áp dụng bước", time_label(step)),
            ("L — thời gian trễ (ước tính)", time_label(model["dead_time_s"])),
            ("τ — hằng số thời gian (ước tính)", time_label(model["time_constant_s"])),
        ]
        references = [("T1 / nhiệt độ đầu mô hình", initial), ("TB cuối đoạn đo", final)]
        end_delay = step + model["dead_time_s"]
        t63 = end_delay + model["time_constant_s"]
        parameters.append(("t63,2% = t đổi u + L + τ", time_label(t63) +
                           (" · ngoài đoạn đo" if t63 > samples[-1][0] else "")))
        parameters.append(("Độ trôi nhiệt độ cuối", f"{model['tail_slope_c_per_min']:+.3f} °C/phút"))
        parameters.append(("Sai số khớp mô hình", f"RMSE {model['fit_rmse_c']:.3f} °C · R² {model['fit_r_squared']:.4f}"))
        markers += [("Hết trễ L", end_delay, initial, "#ae7b21"),
                    ("63,2% mô hình", t63,
                     initial + model["process_gain_c_per_fraction"] * model["power_step"] * (1 - math.exp(-1)),
                     "#8e5ca6")]
        for t, _, _ in samples:
            if t < step:
                continue
            shifted = max(0.0, t - end_delay)
            temperature = initial + model["process_gain_c_per_fraction"] * model["power_step"] * (
                1 - math.exp(-shifted / model["time_constant_s"]))
            modeled.append((t, temperature))
    events = getattr(samples, "step_events", ()) or ((model["step_time_s"],) if fitted else ())
    markers = [("Đổi u" if manual else "Bắt đầu bước", tick, None, "#54687e")
               for tick in events] + markers
    valid = model.get("model_valid_for_pid", False)
    status = ("Đủ tiêu chí kiểm tra mô hình" if valid else
              "CHƯA ĐỦ ĐIỀU KIỆN NHẬN DẠNG" if not fitted else
              "MÔ HÌNH ƯỚC TÍNH · CHƯA ĐẠT")
    write_thermal_svg(
        path, samples, title="Đáp ứng nhiệt theo thời gian",
        subtitle=("Thí nghiệm manual u1 → u2" if manual else "Thí nghiệm công suất cố định") +
                 " · đường đo thực và mô hình được thể hiện riêng",
        control_max=control_max, modeled=modeled, references=references,
        markers=markers, parameters=parameters, status=status, status_ok=valid,
        warnings=model.get("warnings", ()),
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path, help="CSV captured from the controller")
    parser.add_argument("--svg", type=Path, default=Path("heater_response.svg"))
    parser.add_argument("--json", type=Path, default=Path("heater_model.json"))
    parser.add_argument(
        "--allow-unsettled-pid", action="store_true",
        help="vẫn xuất PID ngoại suy khi dữ liệu chưa đạt xác lập (không khuyến nghị)",
    )
    parser.add_argument("--steady-window-s", type=float, default=300.0)
    parser.add_argument("--steady-band-c", type=float, default=1.0)
    parser.add_argument("--steady-slope-c-per-min", type=float, default=0.05)
    args = parser.parse_args()
    try:
        samples = read_samples(args.csv)
    except (OSError, ValueError, KeyError) as exc:
        parser.error(str(exc))
    args.json.parent.mkdir(parents=True, exist_ok=True)
    try:
        model = identify(samples, args.allow_unsettled_pid,
                         steady_window_s=args.steady_window_s,
                         steady_band_c=args.steady_band_c,
                         steady_slope_c_per_min=args.steady_slope_c_per_min)
    except ValueError as exc:
        diagnostic = {"model_valid_for_pid": False, "warnings": [str(exc)]}
        write_svg(args.svg, samples, diagnostic)
        args.json.write_text(json.dumps(diagnostic,
                                        ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"Chưa nhận dạng: {exc}. Đã lưu đồ thị số đo thực.", file=sys.stderr)
        return 2
    if getattr(samples, "manual", False) and samples.final_status not in ("STOP", "DONE"):
        model["warnings"].append(f"Phép đo kết thúc do {samples.final_status}; kiểm tra lại trước khi dùng mô hình.")
        model["model_valid_for_pid"] = False
        for key in model:
            if key.startswith("pid_") or key.startswith("firmware_pid_"):
                model[key] = None
    args.json.write_text(json.dumps(model, indent=2) + "\n", encoding="utf-8")
    write_svg(args.svg, samples, model)
    print(f"u1={model['u1']:.0f}, u2={model['u2']:.0f}, bước tại t={model['step_time_s']:.3f} s")
    print(f"G_u(s) = {model['process_gain_c_per_u']:.6g} exp(-{model['dead_time_s']:.3f}s) / "
          f"({model['time_constant_s']:.3f}s + 1), đầu vào u theo thang 0..{model['control_u_max']}")
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
