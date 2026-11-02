#!/usr/bin/env python3
"""Describe fixed-u trials and compare measured traces, without fitting a model."""

from __future__ import annotations

import argparse
from bisect import bisect_left
import csv
import glob
from html import escape
import json
import math
from pathlib import Path
import statistics
import sys
import textwrap

from analyze_heater_step import linear_slope, read_samples
from thermal_chart import _ticks, clock_label, temperature_bounds, write_thermal_svg


def describe(path, window_s=300.0, band_c=1.0, slope_limit=.05):
    samples = read_samples(path)
    if not samples.manual:
        raise ValueError(f'{path.name}: cần CSV có control_u và control_u_max')
    if samples.step_events or any(not math.isclose(p[1], samples[0][1], abs_tol=1e-9)
                                  for p in samples):
        raise ValueError(f'{path.name}: u thay đổi; chỉ dùng lần đo giữ một u cố định')
    if samples[0][0] < 0 or any(b[0] <= a[0] for a, b in zip(samples, samples[1:])):
        raise ValueError(f'{path.name}: thời gian phải không âm và tăng nghiêm ngặt')
    times = [p[0] for p in samples]
    temperatures = [p[2] for p in samples]
    intervals = [b-a for a, b in zip(times, times[1:])]
    gap_limit = max(5.0, 5 * statistics.median(intervals))
    index = bisect_left(times, times[-1] - window_s)
    # Retain the preceding real sample when the sampling clock misses the boundary.
    if index and times[index] > times[-1] - window_s:
        index -= 1
    tail = samples[index:]
    band = max(p[2] for p in tail) - min(p[2] for p in tail)
    slope = 60 * linear_slope([(t, v) for t, _, v in tail])
    covered = tail[-1][0] - tail[0][0] >= window_s
    gaps = max(intervals) > gap_limit
    completed = samples.final_status in ('STOP', 'DONE')
    warnings = []
    if not covered:
        warnings.append('Chưa đủ thời gian cho cửa sổ đánh giá cuối.')
    if not completed:
        warnings.append(f'Kết thúc {samples.final_status}: chưa xác nhận lần đo hoàn tất bình thường.')
    if gaps:
        warnings.append('Có khoảng trống lấy mẫu dài; kiểm tra CSV trước khi so sánh.')
    if samples.final_status == 'DONE':
        warnings.append('DONE là hết thời gian, không tự có nghĩa nhiệt độ đã xác lập.')
    result = dict(
        file=str(path), label=path.stem, control_u=round(samples[0][1] * samples.control_max),
        control_u_max=samples.control_max, power_percent=100 * samples[0][1],
        sample_count=len(samples), start_s=times[0], end_s=times[-1],
        duration_s=times[-1]-times[0], initial_c=temperatures[0], last_active_c=temperatures[-1],
        min_c=min(temperatures), max_c=max(temperatures), final_status=samples.final_status,
        sample_interval_median_s=statistics.median(intervals), sample_interval_max_s=max(intervals),
        tail_window_s=window_s, tail_start_s=tail[0][0], tail_sample_count=len(tail),
        tail_mean_c=statistics.mean(p[2] for p in tail), tail_range_c=band,
        tail_slope_c_per_min=slope, tail_window_complete=covered,
        tail_criteria_met=covered and band <= band_c + 1e-9 and abs(slope) <= slope_limit,
        tail_assessment_valid=completed and not gaps,
        warnings=warnings,
    )
    return samples, result


def compare(trials):
    """Compare absolute temperatures at shared elapsed times; never extrapolate."""
    if len(trials) < 2:
        return None
    scales = {(r['control_u'], r['control_u_max']) for _, r in trials}
    if len(scales) != 1:
        raise ValueError('So sánh độ lặp lại cần cùng u và cùng thang u ở mọi lần đo')
    start = max(s[0][0] for s, _ in trials)
    end = min(s[-1][0] for s, _ in trials)
    if end <= start:
        raise ValueError('Các lần đo không có khoảng thời gian chung để so sánh')
    grid = [start + i * 10 for i in range(int((end-start) // 10) + 1)]
    if grid[-1] < end:
        grid.append(end)
    series = [[p[0] for p in s] for s, _ in trials]
    values = []
    for t in grid:
        row = []
        for (samples, report), times in zip(trials, series):
            index = bisect_left(times, t)
            if index < len(times) and times[index] == t:
                value = samples[index][2]
            else:
                a, b = samples[index-1], samples[index]
                if b[0] - a[0] > max(5, 5 * report['sample_interval_median_s']):
                    break  # Do not interpolate through a long acquisition gap.
                value = a[2] + (b[2]-a[2]) * (t-a[0]) / (b[0]-a[0])
            row.append(value)
        if len(row) == len(trials):
            values.append(row)
    if not values:
        raise ValueError('Không có mốc chung đủ dữ liệu để so sánh')
    ranges = [max(row)-min(row) for row in values]
    deviations = [statistics.stdev(row) for row in values]
    pairs = []
    for i in range(len(trials)):
        for j in range(i+1, len(trials)):
            pairs.append(dict(first=trials[i][1]['file'], second=trials[j][1]['file'],
                              rmse_c=math.sqrt(statistics.mean((r[i]-r[j])**2 for r in values))))
    return dict(common_start_s=start, common_end_s=end, grid_interval_s=10,
                compared_time_count=len(values), skipped_time_count=len(grid)-len(values),
                mean_between_run_range_c=statistics.mean(ranges),
                max_between_run_range_c=max(ranges),
                mean_between_run_sample_sd_c=statistics.mean(deviations),
                pairwise_rmse=pairs,
                method='Nội suy tuyến tính trong khoảng thời gian chung, mỗi 10 s; '
                       'giữ nhiệt độ tuyệt đối, không dịch đường để khớp; bỏ mốc nằm trong khoảng mất mẫu dài.',
                assessment='Chỉ mô tả độ khác nhau; chưa tự kết luận độ lặp lại đạt/không đạt.')


def write_comparison_svg(path, trials, comparison, warnings, temperature_limits=(0, 140)):
    colors = ('#1765bb', '#b54b32', '#21835c', '#8b57ae', '#a47b13', '#197b8b')
    width = 1360
    legend_rows = math.ceil(len(trials)/2)
    top = 145 + legend_rows*24
    bottom = top + 380
    table_y = bottom + 125
    notes_y = table_y + 48 + len(trials)*32
    note_lines = [line for warning in warnings for line in textwrap.wrap(warning, 135)]
    height = notes_y + 95 + 20*len(note_lines)
    xmax = max(s[-1][0] for s, _ in trials)
    temps = [p[2] for s, _ in trials for p in s]
    low, high = temperature_bounds(temps, temperature_limits)
    x = lambda t: 90 + 1180*t/max(1, xmax)
    y = lambda v: bottom - (v-low)/(high-low)*(bottom-top)
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" data-temperature-min-c="{low:g}" data-temperature-max-c="{high:g}">',
             '<title>So sánh các lần đo giữ u cố định</title>',
             '<desc>Các đường là nhiệt độ đo thực, cùng mốc thời gian từ đầu log. Không khớp hàm truyền.</desc>',
             '<style>text{font-family:Arial,"DejaVu Sans",sans-serif;fill:#243447}.point{fill-opacity:0;pointer-events:all}.point:hover{fill-opacity:1;stroke:white;stroke-width:2}</style>',
             '<rect width="100%" height="100%" fill="#f4f7fb"/>',
             f'<rect x="55" y="{top-15}" width="1245" height="{bottom-top+85}" rx="12" fill="white"/>']
    def text(tx, ty, value, size=14, extra=''):
        parts.append(f'<text x="{tx}" y="{ty}" font-size="{size}" {extra}>{escape(str(value))}</text>')
    u = trials[0][1]['control_u']; maximum = trials[0][1]['control_u_max']
    text(60, 40, f'So sánh {len(trials)} lần đo · u = {u}/{maximum} ({100*u/maximum:.2f}%)', 25, 'font-weight="700"')
    text(60, 68, 'Nhiệt độ đo thực · cùng thời gian từ đầu log · không dịch nhiệt độ ban đầu để làm trùng đường')
    text(60, 95, f"Khoảng chung: {clock_label(comparison['common_start_s'])}–{clock_label(comparison['common_end_s'])}; "
         f"chênh giữa các lần: TB {comparison['mean_between_run_range_c']:.2f} °C, lớn nhất {comparison['max_between_run_range_c']:.2f} °C")
    text(1270, top-25, f'Thang nhiệt: {low:g}–{high:g} °C', 12, 'text-anchor="end"')
    for i, (_, r) in enumerate(trials):
        color = colors[i % len(colors)]; lx = 65 + (i % 2)*635; ly = 125 + (i//2)*24
        parts.append(f'<line x1="{lx}" y1="{ly-4}" x2="{lx+25}" y2="{ly-4}" stroke="{color}" stroke-width="3"/>')
        label = f"{i+1}. {Path(r['file']).parent.name}/{Path(r['file']).name}"
        text(lx+32, ly, label if len(label)<=72 else label[:69]+'…', 12)
    for value in _ticks(low, high, count=7):
        parts.append(f'<line x1="90" y1="{y(value):.1f}" x2="1270" y2="{y(value):.1f}" stroke="#e0e7ef"/>')
        text(55, y(value)+4, f'{value:g}', 12)
    for i in range(7):
        t = xmax*i/6
        parts.append(f'<line x1="{x(t):.1f}" y1="{top}" x2="{x(t):.1f}" y2="{bottom}" stroke="#e0e7ef"/>')
        text(x(t), bottom+25, clock_label(t), 12, 'text-anchor="middle"')
    text(680, bottom+52, 'Thời gian từ đầu log (phút:giây)', 14, 'text-anchor="middle"')
    text(27, top+200, 'Nhiệt độ (°C)', 14, f'transform="rotate(-90 27 {top+200})"')
    for i, (samples, r) in enumerate(trials):
        color = colors[i % len(colors)]
        measured = ' '.join(f'{x(t):.1f},{y(v):.1f}' for t, _, v in samples)
        parts.append(f'<polyline data-run="{i+1}" points="{measured}" fill="none" stroke="{color}" stroke-width="1.6"/>')
        for t, _, v in samples:
            tip = f'Lần {i+1}; t={t:.3f} s ({clock_label(t)}); T={v:.2f} °C; u={u}'
            parts.append(f'<circle class="point" cx="{x(t):.1f}" cy="{y(v):.1f}" r="4" fill="{color}"><title>{escape(tip)}</title></circle>')
    text(60, table_y-18, 'Đánh giá cửa sổ cuối của từng lần (có thể ở các thời điểm kết thúc khác nhau)', 16, 'font-weight="700"')
    columns = [(65,'Lần'),(130,'Thời gian'),(285,'T đầu'),(405,'T cuối RUN'),(555,'TB cuối'),(700,'Dải cuối'),(845,'Dốc °C/phút'),(1025,'Tiêu chí cuối')]
    for xx, title in columns:
        text(xx, table_y+12, title, 13, 'font-weight="700"')
    for i, (_, r) in enumerate(trials):
        status = tail_label(r)
        values = [str(i+1), clock_label(r['duration_s']), f"{r['initial_c']:.1f} °C", f"{r['last_active_c']:.1f} °C",
                  f"{r['tail_mean_c']:.2f} °C", f"{r['tail_range_c']:.2f} °C", f"{r['tail_slope_c_per_min']:+.3f}", status]
        for (xx, _), value in zip(columns, values):
            text(xx, table_y+43+i*32, value, 13)
    text(60, notes_y+10, 'Các chỉ số so sánh dùng cùng khoảng thời gian; bảng cuối từng lần không dùng để thay thế so sánh đó.', 12)
    text(60, notes_y+32, 'Đường lặp lại tốt vẫn có thể còn tăng. Đạt tiêu chí một cửa sổ cuối chưa chứng minh cân bằng lâu dài.', 12)
    for i, line in enumerate(note_lines):
        text(60, notes_y+60+i*20, line, 12)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('\n'.join(parts+['</svg>'])+'\n', encoding='utf-8')


def tail_label(report):
    if not report['tail_assessment_valid']:
        return 'Cần kiểm tra log'
    return 'Đạt cửa sổ cuối' if report['tail_criteria_met'] else 'Chưa đạt cửa sổ cuối'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv', nargs='+', help='một/nhiều CSV, hỗ trợ mẫu tên file như "data/run_*/manual_step.csv"')
    parser.add_argument('--svg', type=Path)
    parser.add_argument('--json', type=Path)
    parser.add_argument('--summary-csv', type=Path)
    parser.add_argument('--steady-window-s', type=float, default=300)
    parser.add_argument('--steady-band-c', type=float, default=1)
    parser.add_argument('--steady-slope-c-per-min', type=float, default=.05)
    scale = parser.add_mutually_exclusive_group()
    scale.add_argument('--temperature-range-c', type=float, nargs=2, metavar=('MIN', 'MAX'),
                       default=(0, 140), help='thang nhiệt tổng quan (mặc định 0 140); tự mở rộng nếu số đo nằm ngoài')
    scale.add_argument('--auto-temperature-range', action='store_true',
                       help='phóng trục nhiệt theo min/max số đo thay vì thang tổng quan')
    args = parser.parse_args(argv)
    temperature_limits = None if args.auto_temperature_range else args.temperature_range_c
    if temperature_limits is not None and (not all(math.isfinite(v) for v in temperature_limits)
                                           or temperature_limits[0] >= temperature_limits[1]):
        parser.error('Thang nhiệt độ phải hữu hạn và min nhỏ hơn max')
    limits = (args.steady_window_s, args.steady_band_c, args.steady_slope_c_per_min)
    if any(not math.isfinite(v) or v <= 0 for v in limits):
        parser.error('Ngưỡng cửa sổ, dải nhiệt và độ dốc phải hữu hạn và lớn hơn 0')
    paths = []
    for pattern in args.csv:
        matches = sorted(glob.glob(pattern))
        if not matches:
            parser.error(f'Không tìm thấy CSV: {pattern}')
        paths.extend(Path(p) for p in matches)
    if len({p.resolve() for p in paths}) != len(paths):
        parser.error('Một CSV được chọn nhiều lần; không tính cùng dữ liệu thành nhiều lần thử')
    try:
        trials = [describe(p, *limits) for p in paths]
        comparison = compare(trials)
    except (ValueError, OSError, KeyError, csv.Error) as exc:
        parser.error(str(exc))
    warnings = ['So sánh cần cùng lượng nước/tải, vị trí cảm biến, nhiệt độ và cách gia nhiệt ban đầu; CSV không xác nhận được các điều kiện này.']
    initial_range = max(r['initial_c'] for _, r in trials)-min(r['initial_c'] for _, r in trials)
    if len(trials) > 1 and initial_range > 2:
        warnings.append(f'Nhiệt độ đầu chênh {initial_range:.1f} °C; cần xét khác điều kiện ban đầu khi diễn giải độ lặp lại.')
    for i, (_, r) in enumerate(trials, 1):
        warnings.extend(f'Lần {i}: {w}' for w in r['warnings'])
    base = paths[0].with_suffix('') if len(paths)==1 else Path('repeatability')
    svg_path = args.svg or base.with_name(base.name+'.svg')
    json_path = args.json or base.with_name(base.name+'_summary.json')
    csv_path = args.summary_csv or base.with_name(base.name+'_summary.csv')
    outputs = (svg_path, json_path, csv_path)
    if len({p.resolve() for p in outputs}) != 3 or any(p.resolve() in {q.resolve() for q in paths} for p in outputs):
        parser.error('Các file kết quả phải khác nhau và không được ghi đè CSV đầu vào')
    result = dict(analysis='fixed_u_repeatability', run_count=len(trials),
                  chart_temperature_range_c=temperature_bounds([p[2] for s, _ in trials for p in s], temperature_limits),
                  thresholds=dict(window_s=limits[0], range_c=limits[1], abs_slope_c_per_min=limits[2]),
                  runs=[r for _, r in trials], comparison=comparison, warnings=warnings,
                  notes=['Chỉ đánh giá số đo; không khớp hàm truyền hoặc tính PID.',
                         'Nhiệt độ cuối và thời gian đồ thị dùng mẫu RUN cuối, không dùng bản ghi STOP/DONE.'])
    if len(trials) == 1:
        samples, r = trials[0]
        write_thermal_svg(svg_path, samples, title='Thí nghiệm giữ u cố định',
            subtitle='Nhiệt độ đo thực theo thời gian · đánh giá cửa sổ cuối · không khớp hàm truyền',
            control_max=r['control_u_max'], temperature_limits=temperature_limits, parameters=[
                ('Công suất giữ nguyên', f"u={r['control_u']}/{r['control_u_max']} ({r['power_percent']:.2f}%)"),
                ('Trạng thái kết thúc', str(r['final_status'])),
                ('Cửa sổ cuối', f"{limits[0]:g} s; thực có {samples[-1][0]-r['tail_start_s']:.1f} s"),
                ('Trung bình cuối (số đo)', f"{r['tail_mean_c']:.2f} °C"),
                ('Dải nhiệt max−min cuối', f"{r['tail_range_c']:.2f} °C / ngưỡng {limits[1]:g} °C"),
                ('Độ dốc hồi quy cuối', f"{r['tail_slope_c_per_min']:+.3f} °C/phút"),
                ('Ngưỡng |độ dốc|', f"≤{limits[2]:g} °C/phút"),
            ], status=tail_label(r).upper(),
            status_ok=r['tail_criteria_met'] and r['tail_assessment_valid'], warnings=warnings)
    else:
        write_comparison_svg(svg_path, trials, comparison, warnings, temperature_limits)
    for p in (json_path, csv_path):
        p.parent.mkdir(parents=True, exist_ok=True)
    json_path.write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    fields = [k for k in trials[0][1] if k != 'warnings']
    with csv_path.open('w', newline='', encoding='utf-8-sig') as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, extrasaction='ignore')
        writer.writeheader()
        writer.writerows(r for _, r in trials)
    print(f'Đã phân tích {len(trials)} lần đo giữ u cố định; không yêu cầu bước đổi u.')
    for i, (_, r) in enumerate(trials, 1):
        print(f"Lần {i}: u={r['control_u']}; cuối RUN {r['last_active_c']:.1f} °C; "
              f"dốc {r['tail_slope_c_per_min']:+.3f} °C/phút; {tail_label(r)}.")
    print(f'Đồ thị: {svg_path}\nJSON: {json_path}\nBảng thống kê: {csv_path}')
    for warning in warnings:
        print(f'LƯU Ý: {warning}', file=sys.stderr)
    # Unsettled temperature is a valid measured result, not an acquisition failure.
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
