"""Standalone SVG thermal charts with measured-value tooltips (stdlib only)."""

import math
import textwrap
from html import escape
from pathlib import Path


def clock_label(seconds: float) -> str:
    """Format absolute elapsed seconds; keep minutes above 59 unambiguous."""
    sign = '-' if seconds < 0 else ''
    minutes, remainder = divmod(int(round(abs(seconds))), 60)
    return f'{sign}{minutes:02d}:{remainder:02d}'


def time_label(seconds: float) -> str:
    return f'{seconds:.3f} s ({clock_label(seconds)})'


def _ticks(low: float, high: float, count: int = 5):
    raw = max(high - low, 0.1) / count
    scale = 10 ** math.floor(math.log10(raw))
    step = next(value * scale for value in (1, 2, 5, 10) if value * scale >= raw)
    first = math.ceil(low / step) * step
    return [first + i * step for i in range(int((high - first) / step) + 1)]


def temperature_bounds(values, limits=None):
    """Use an overview range when requested, expanding to keep every value visible."""
    low, high = min(values), max(values)
    if limits is None:
        pad = max(0.5, (high-low)*0.12)
        return low-pad, high+pad
    minimum, maximum = limits
    if not all(math.isfinite(v) for v in limits) or minimum >= maximum:
        raise ValueError('Thang nhiệt độ phải hữu hạn và min nhỏ hơn max')
    if low < minimum:
        minimum = math.floor(low/10)*10
    if high > maximum:
        maximum = math.ceil(high/10)*10
    return minimum, maximum


def write_thermal_svg(path: Path, samples, *, title: str, subtitle: str,
                      control_max: int = 255, modeled=(), references=(),
                      markers=(), parameters=(), status='', status_ok=False,
                      warnings=(), temperature_limits=None) -> None:
    """Render raw measurements separately from model estimates and metadata.

    modeled: (time_s, temperature_c); references: (label, temperature_c);
    markers: (label, absolute_time_s, optional_temperature_c, color).
    All times retain the acquisition clock; no temperature or time is inferred
    for a measured point. SVG titles expose the exact values in a browser.
    """
    if not samples:
        raise ValueError('Không có mẫu nhiệt độ để vẽ')
    modeled = list(modeled)
    width, height = 1360, 990
    left, right, top, bottom = 90, 970, 230, 595
    panel_left, panel_width = 1005, 325
    power_top, power_bottom = 745, 855
    xmin = min(p[0] for p in samples)
    xmax = max(max(p[0] for p in samples), xmin + 1)
    temperatures = [p[2] for p in samples]
    extent = temperatures + [p[1] for p in modeled if xmin <= p[0] <= xmax]
    ymin, ymax = temperature_bounds(extent, temperature_limits)

    def x(t):
        return left + (t - xmin) * (right - left) / (xmax - xmin)

    def y(value):
        return bottom - (value - ymin) * (bottom - top) / (ymax - ymin)

    def power_y(value):
        return power_bottom - value * (power_bottom - power_top)

    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" data-temperature-min-c="{ymin:g}" data-temperature-max-c="{ymax:g}">',
             f'<title>{escape(title)}</title>',
             f'<desc>{escape(subtitle)}. Blue: actual PT100 readings; dashed red: model estimates. Each measured point has time, temperature and applied u in its tooltip.</desc>',
             '<style>text{font-family:Arial,"DejaVu Sans",sans-serif;fill:#243447} .muted{fill:#61758b} .small{font-size:12px} .hover-point{fill:#1765bb;fill-opacity:0;stroke-opacity:0;pointer-events:all} .hover-point:hover{fill-opacity:1;stroke:white;stroke-width:2;stroke-opacity:1} .value-label{paint-order:stroke;stroke:#fff;stroke-width:5px;stroke-linejoin:round}</style>',
             '<defs><clipPath id="temperature-clip"><rect x="90" y="230" width="880" height="365"/></clipPath></defs>',
             '<rect width="100%" height="100%" fill="#f4f7fb"/>',
             '<rect x="60" y="195" width="935" height="710" rx="12" fill="white"/>',
             f'<rect x="{panel_left}" y="195" width="{panel_width}" height="710" rx="12" fill="white"/>']

    def text(tx, ty, value, size=14, color=None, anchor='start', extra=''):
        style = f' style="fill:{color}"' if color else ''
        parts.append(f'<text x="{tx:.1f}" y="{ty:.1f}" font-size="{size}" text-anchor="{anchor}"{style} {extra}>{escape(str(value))}</text>')

    text(60, 38, title, 25, extra='font-weight="700"')
    text(60, 65, subtitle, 14, '#61758b')
    first, last = samples[0], samples[-1]
    cards = [
        ('NHIỆT ĐỘ ĐẦU', f'{first[2]:.1f} °C', f't = {time_label(first[0])}'),
        ('NHIỆT ĐỘ CUỐI', f'{last[2]:.1f} °C', f't = {time_label(last[0])}'),
        ('THỜI GIAN GHI MẪU', clock_label(last[0] - first[0]),
         f'{last[0] - first[0]:.3f} s · {len(samples)} mẫu'),
        ('MIN / MAX ĐO ĐƯỢC', f'{min(temperatures):.1f} / {max(temperatures):.1f} °C',
         f'Chênh đầu–cuối: {last[2] - first[2]:+.1f} °C'),
    ]
    for i, (label, value, note) in enumerate(cards):
        card_x = 60 + 320 * i
        parts.append(f'<rect x="{card_x}" y="90" width="310" height="85" rx="10" fill="white"/>')
        text(card_x + 16, 112, label, 11, '#61758b', extra='font-weight="700"')
        text(card_x + 16, 140, value, 23, extra='font-weight="700"')
        text(card_x + 16, 161, note, 12, '#61758b')

    parts.append('<line x1="90" y1="214" x2="116" y2="214" stroke="#1765bb" stroke-width="3"/>')
    text(123, 218, 'Nhiệt độ đo thực', 13)
    if modeled:
        parts.append('<line x1="300" y1="214" x2="326" y2="214" stroke="#cf4b45" stroke-width="2" stroke-dasharray="7 4"/>')
        text(333, 218, 'Mô hình ước tính', 13)
    scale_label = f'Thang nhiệt: {ymin:g}–{ymax:g} °C' if temperature_limits is not None else 'Đơn vị: °C'
    text(960, 218, scale_label, 12, '#61758b', 'end')

    for value in _ticks(ymin, ymax, count=7 if temperature_limits is not None else 5):
        parts.append(f'<line x1="{left}" y1="{y(value):.1f}" x2="{right}" y2="{y(value):.1f}" stroke="#e5ebf2"/>')
        text(left - 12, y(value) + 4, f'{value:g}', 12, '#61758b', 'end')
    for i in range(7):
        tick = xmin + (xmax - xmin) * i / 6
        for a, b in ((top, bottom), (power_top, power_bottom)):
            parts.append(f'<line x1="{x(tick):.1f}" y1="{a}" x2="{x(tick):.1f}" y2="{b}" stroke="#e5ebf2"/>')
        text(x(tick), 620, clock_label(tick), 13, anchor='middle')
        text(x(tick), 638, f'({tick:.1f} s)', 11, '#61758b', 'middle')
        text(x(tick), 876, clock_label(tick), 12, '#61758b', 'middle')
    for a, b in ((top, bottom), (power_top, power_bottom)):
        parts.append(f'<path d="M{left} {a}V{b}H{right}" fill="none" stroke="#9aaabd"/>')
    text(25, 435, 'Nhiệt độ (°C)', 14, extra='transform="rotate(-90 25 435)"')
    text((left + right) / 2, 663, 'Thời gian của log: phút:giây (số giây trong ngoặc)', 13, anchor='middle')

    for label, value in references:
        if ymin <= value <= ymax:
            parts.append(f'<line x1="{left}" y1="{y(value):.1f}" x2="{right}" y2="{y(value):.1f}" stroke="#91a4b9" stroke-dasharray="3 5"><title>{escape(label)} = {value:.2f} °C</title></line>')

    parts.append('<g clip-path="url(#temperature-clip)">')
    measured = ' '.join(f'{x(t):.1f},{y(temp):.1f}' for t, _, temp in samples)
    parts.append(f'<polyline points="{measured}" fill="none" stroke="#1765bb" stroke-width="2"/>')
    if modeled:
        estimated = ' '.join(f'{x(t):.1f},{y(temp):.1f}' for t, temp in modeled)
        parts.append(f'<polyline points="{estimated}" fill="none" stroke="#cf4b45" stroke-width="2" stroke-dasharray="8 5"/>')
    parts.append('</g>')

    for i, (label, tick, temperature, color) in enumerate(markers):
        if xmin <= tick <= xmax:
            parts.append(f'<line data-marker="{escape(label, quote=True)}" data-time-s="{tick:.6f}" x1="{x(tick):.1f}" y1="{top}" x2="{x(tick):.1f}" y2="{bottom}" stroke="{color}" stroke-dasharray="5 5" opacity="0.65"><title>{escape(label)}: {time_label(tick)}</title></line>')
            parts.append(f'<line x1="{x(tick):.1f}" y1="{power_top}" x2="{x(tick):.1f}" y2="{power_bottom}" stroke="{color}" stroke-dasharray="5 5" opacity="0.65"/>')
            anchor = 'end' if x(tick) > (left + right) / 2 else 'start'
            label_x = x(tick) - 7 if anchor == 'end' else x(tick) + 7
            text(label_x, top + 20 + 23 * (i % 3), f'{label}: {clock_label(tick)}', 12, color, anchor, 'class="value-label"')
            if temperature is not None and ymin <= temperature <= ymax:
                parts.append(f'<circle cx="{x(tick):.1f}" cy="{y(temperature):.1f}" r="5" fill="{color}" stroke="white" stroke-width="2"><title>{escape(label)}: {temperature:.2f} °C; {time_label(tick)}</title></circle>')

    for sample, anchor, offset in ((first, 'start', 10), (last, 'end', -10)):
        tick, _, temp = sample
        parts.append(f'<circle cx="{x(tick):.1f}" cy="{y(temp):.1f}" r="4" fill="#1765bb"/>')
        text(x(tick) + offset, max(top + 14, y(temp) - 12), f'{temp:.1f} °C · {clock_label(tick)}', 13, '#1765bb', anchor, 'class="value-label"')

    # A browser shows the exact measurement on hover; these are never interpolated.
    for tick, power, temp in samples:
        tip = f't = {time_label(tick)}; T = {temp:.2f} °C; u = {power * control_max:.3f} ({power * 100:.2f}%)'
        parts.append(f'<circle class="hover-point" data-time-s="{tick:.6f}" data-temperature-c="{temp:.6f}" cx="{x(tick):.1f}" cy="{y(temp):.1f}" r="5"><title>{escape(tip)}</title></circle>')

    text(left, 714, 'CÔNG SUẤT ÁP DỤNG', 13, extra='font-weight="700"')
    text(right, 714, f'u theo thang 0–{control_max}', 12, '#61758b', 'end')
    for power in (0, .5, 1):
        parts.append(f'<line x1="{left}" y1="{power_y(power):.1f}" x2="{right}" y2="{power_y(power):.1f}" stroke="#e5ebf2"/>')
        text(left - 12, power_y(power) + 4, f'{power * control_max:g}', 11, '#61758b', 'end')
    command = [f'M{x(first[0]):.1f} {power_y(first[1]):.1f}']
    for tick, power, _ in samples[1:]:
        command.append(f'H{x(tick):.1f}V{power_y(power):.1f}')
    parts.append(f'<path d="{" ".join(command)}" fill="none" stroke="#248564" stroke-width="2.5"/>')
    # Label the beginning of each constant-u segment; fitting still uses the raw data.
    segments = [samples[0]] + [b for a, b in zip(samples, samples[1:]) if not math.isclose(a[1], b[1], abs_tol=1e-9)]
    for tick, power, _ in segments:
        text(min(x(tick) + 7, right - 5), max(power_top + 14, power_y(power) - 9),
             f'u={power * control_max:g} ({power * 100:.1f}%)', 12, '#248564',
             'end' if x(tick) > right - 160 else 'start', 'class="value-label"')

    text(panel_left + 20, 222, 'THÔNG SỐ & MỐC THỜI GIAN', 14, extra='font-weight="700"')
    cursor = 253
    for label, value in parameters:
        for line in textwrap.wrap(str(label), width=37):
            text(panel_left + 20, cursor, line, 12, '#61758b')
            cursor += 17
        cursor += 3
        for line in textwrap.wrap(str(value), width=34):
            text(panel_left + 20, cursor, line, 14, extra='font-weight="600"')
            cursor += 19
        cursor += 13
    color = '#227659' if status_ok else '#a15c19'
    status_y = max(730, cursor + 5)
    text(panel_left + 20, status_y, status, 13, color, extra='font-weight="700"')
    warning_lines = []
    for warning in warnings:
        warning_lines.extend(textwrap.wrap(str(warning), width=39))
    displayed_warnings = warning_lines[:7]
    if len(warning_lines) > 7:
        displayed_warnings[-1] = 'Xem đầy đủ cảnh báo trong file JSON.'
    for i, line in enumerate(displayed_warnings):
        text(panel_left + 20, status_y + 22 + 17 * i, line, 12, '#61758b')
    # Grow the document if metadata exceeds the default panel, rather than hide it.
    content_bottom = max(905, status_y + 35 + 17 * min(len(warning_lines), 7))
    if content_bottom > 905:
        extra_height = content_bottom - 905
        parts[0] = parts[0].replace(f'height="{height}"', f'height="{height + extra_height}"').replace(f'viewBox="0 0 {width} {height}"', f'viewBox="0 0 {width} {height + extra_height}"')
    text(60, content_bottom + 29, 'Rê chuột lên điểm đo trong file SVG để xem thời gian, nhiệt độ và u chính xác.', 13, '#61758b')
    text(60, content_bottom + 51, 'Nhiệt độ cuối / trung bình cuối chỉ là số đo; chỉ gọi là xác lập khi đạt tiêu chí kiểm tra.', 12, '#61758b')
    parts.append('</svg>')
    if content_bottom > 905:
        parts = [part.replace(f'x="{panel_left}" y="195" width="{panel_width}" height="710"',
                              f'x="{panel_left}" y="195" width="{panel_width}" height="{content_bottom - 195}"')
                 for part in parts]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('\n'.join(parts) + '\n', encoding='utf-8')
