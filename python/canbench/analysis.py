"""Trace diagnosis and exported signal plots. No connection to an external service."""
import json
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from .logs import Frame, statistics_report
from .signals import Description, decode_frame

ABORTS = {0x06090030: 'value outside allowed range', 0x06010002: 'read-only object',
          0x06020000: 'object does not exist', 0x06070010: 'type or length mismatch',
          0x05040001: 'unsupported command', 0x08000022: 'device state prevents transfer'}


def diagnose(frames: list[Frame], description: Description, bitrate: int = 500000) -> dict:
    stats = statistics_report(frames, bitrate)
    findings = []
    for frame in frames:
        if frame.error:
            findings.append(f'{frame.timestamp:.6f}s: CAN error frame 0x{frame.can_id:08X}')
            continue
        if frame.remote:
            continue
        if 0x580 <= frame.can_id <= 0x5FF and len(frame.data) == 8 and frame.data[0] == 0x80:
            code = int.from_bytes(frame.data[4:8], 'little')
            index = int.from_bytes(frame.data[1:3], 'little')
            findings.append(f'{frame.timestamp:.6f}s: SDO abort 0x{code:08X} at 0x{index:04X}:{frame.data[3]:02X}: {ABORTS.get(code, "unknown abort code")}')
        try:
            values = decode_frame(frame, description)
        except ValueError as exc:
            findings.append(f'{frame.timestamp:.6f}s: malformed message 0x{frame.can_id:03X}: {exc}')
            continue
        for signal in description.messages.get(frame.can_id, ()):
            value = values[signal.name]
            if (signal.minimum is not None and value < signal.minimum) or (signal.maximum is not None and value > signal.maximum):
                findings.append(f'{frame.timestamp:.6f}s: {signal.name} out of range: {value:g} {signal.unit}')
    if frames:
        start, end = frames[0].timestamp, frames[-1].timestamp
        for identifier, timeout in description.heartbeat_ms.items():
            times = [f.timestamp for f in frames if f.can_id == identifier and not f.error and not f.remote and len(f.data) == 1]
            boundaries = [start, *times, end]
            gaps = [1000 * (b - a) for a, b in zip(boundaries, boundaries[1:])]
            if gaps and max(gaps) > timeout:
                findings.append(f'Missing heartbeat 0x{identifier:03X}: observed gap {max(gaps):.1f} ms exceeds {timeout:g} ms within capture')
    return {'statistics': stats, 'findings': findings}


def write_report(result: dict, path: Path) -> None:
    stats = result['statistics']
    load = stats['estimated_load_percent']
    upper = stats['estimated_load_upper_percent']
    lines = ['# CAN bench diagnosis', '', f'- Frames: {stats["frames"]}', f'- Capture span: {stats["duration_s"]:.6f} s',
             f'- Error frames: {stats["error_frames"]}',
             f'- Estimated data/RTR bus load: {load:.3f}% to {upper:.3f}%' if load is not None else '- Bus load: unavailable for a zero-duration capture',
             '', '## Findings', '']
    lines += [f'- {finding}' for finding in result['findings']] or ['- No configured problem was detected within the capture window.']
    lines += ['', '## Message timing', '', '| CAN id | Count | Rate Hz | Mean interval ms | Jitter stddev ms |', '| --- | ---: | ---: | ---: | ---: |']
    for identifier, item in stats['by_id'].items():
        values = [str(item['count']), *(f'{item[k]:.3f}' if item[k] is not None else 'n/a' for k in ('rate_hz', 'mean_interval_ms', 'jitter_stddev_ms'))]
        lines.append('| ' + identifier + ' | ' + ' | '.join(values) + ' |')
    lines += ['', '## Limits', '', 'Classic 11 bit CAN only. Load is a frame-bit estimate at the configured nominal bitrate, with no-stuffing and worst-case stuffing bounds. Error-frame occupancy, unlogged traffic and retransmissions are excluded. vcan has no physical bitrate or arbitration timing.',
              'Rate uses count divided by the whole observed capture span. Jitter is the population standard deviation of interarrival intervals. Missing heartbeats are checked only inside the observed window; silence after its end cannot be inferred.', '']
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('\n'.join(lines), encoding='utf-8')


def plot_signal(frames: list[Frame], description: Description, name: str, path: Path) -> None:
    samples = []
    unit = ''
    for frame in frames:
        try:
            values = decode_frame(frame, description)
        except ValueError:
            continue
        if name in values:
            samples.append((frame.timestamp, values[name]))
            unit = next(s.unit for s in description.messages[frame.can_id] if s.name == name)
    if not samples:
        raise ValueError(f'No samples for signal {name}')
    origin = frames[0].timestamp
    figure, axes = plt.subplots(figsize=(8, 4), constrained_layout=True)
    axes.step([t - origin for t, _ in samples], [v for _, v in samples], where='post', marker='o', markersize=3)
    axes.set(xlabel='Time from capture start (s)', ylabel=f'{name} ({unit})' if unit else name, title=f'Synthetic lifter: {name}')
    axes.grid(alpha=.25)
    path.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(path, dpi=160)
    plt.close(figure)


def write_analysis(frames, description, directory, signal='position', bitrate=500000):
    result = diagnose(frames, description, bitrate)
    directory.mkdir(parents=True, exist_ok=True)
    write_report(result, directory / 'diagnosis.md')
    (directory / 'statistics.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    plot_signal(frames, description, signal, directory / 'signal.png')
    return result
