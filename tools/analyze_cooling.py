#!/usr/bin/env python3
"""Plot a 0% cooling log and identify its natural-cooling model."""

import argparse
import json
import math
from pathlib import Path

from analyze_heater_step import read_samples
from analyze_thermal_model import fit_cooling
from thermal_chart import time_label, write_thermal_svg


def write_svg(path: Path, samples, model) -> None:
    start = samples[0][0]
    ambient = model["ambient_temperature_c"]
    initial = model["cooling_initial_temperature_c"]
    tau = model["time_constant_s"]
    modeled = [(t, ambient + (initial - ambient) * math.exp(-(t - start) / tau))
               for t, _, _ in samples]
    parameters = [
        ("Công suất", "0% · nguội tự nhiên"),
        ("Nhiệt độ đầu mô hình", f"{initial:.2f} °C"),
        ("Nhiệt độ môi trường (ước tính)", f"{ambient:.2f} °C"),
        ("τ — hằng số thời gian nguội", time_label(tau)),
        ("Thời điểm t đầu + τ", time_label(start + tau) +
         (" · ngoài đoạn đo" if start + tau > samples[-1][0] else "")),
        ("Sai số mô hình", f"RMSE {model['cooling_rmse_c']:.3f} °C · R² {model['cooling_r_squared']:.4f}"),
    ]
    write_thermal_svg(
        path, samples, title="Đường nhiệt độ nguội tự nhiên",
        subtitle="Natural cooling response (0%) · nhiệt độ thực đo theo thời gian",
        control_max=getattr(samples, "control_max", 255), modeled=modeled,
        references=[("Nhiệt độ môi trường ước tính", ambient)],
        markers=[("Sau một τ", start + tau, ambient + (initial - ambient) / math.e, "#8e5ca6")],
        parameters=parameters, status="MÔ HÌNH NGUỘI ƯỚC TÍNH",
        warnings=["Thông số môi trường và τ được khớp từ đường nguội; không phải nhiệt độ đo trực tiếp."],
    )


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
