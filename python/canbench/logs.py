"""Read classic, timestamped candump compact and bracket formats."""
from dataclasses import dataclass
import math
from pathlib import Path
import re
import statistics


@dataclass(frozen=True)
class Frame:
    timestamp: float
    channel: str
    can_id: int
    data: bytes
    error: bool = False
    remote: bool = False
    requested_dlc: int = 0

    def __post_init__(self):
        if not math.isfinite(self.timestamp) or self.timestamp < 0:
            raise ValueError('Timestamp must be finite and nonnegative')
        if not 0 <= self.can_id <= (0x1FFFFFFF if self.error else 0x7FF):
            raise ValueError('Only standard 11 bit data ids are supported')
        if len(self.data) > 8 or not 0 <= self.requested_dlc <= 8:
            raise ValueError('Classic CAN DLC must be 0 through 8')
        if self.remote and self.data:
            raise ValueError('Remote frames cannot contain payload data')

    @property
    def dlc(self):
        return self.requested_dlc if self.remote else len(self.data)

    def candump(self):
        identifier = f'{self.can_id | 0x20000000:08X}' if self.error else f'{self.can_id:03X}'
        payload = f'R{self.dlc}' if self.remote else self.data.hex().upper()
        return f'({self.timestamp:.6f}) {self.channel} {identifier}#{payload}'


COMPACT = re.compile(r'^\((\d+(?:\.\d+)?)\)\s+(\S+)\s+([0-9A-Fa-f]{3,8})#([0-9A-Fa-f]*|R[0-8]?)$')
BRACKET = re.compile(r'^\((\d+(?:\.\d+)?)\)\s+(\S+)\s+([0-9A-Fa-f]{3,8})\s+\[([0-8])\]\s*(.*)$')


def parse_line(line: str) -> Frame:
    match = COMPACT.fullmatch(line.strip())
    if match:
        timestamp, channel, identifier, payload = match.groups()
        raw_id = int(identifier, 16)
        remote = payload.startswith('R')
        dlc = int(payload[1:] or '0') if remote else 0
        data = b'' if remote else bytes.fromhex(payload)
    else:
        match = BRACKET.fullmatch(line.strip())
        if not match:
            raise ValueError('Expected timestamped classic candump data')
        timestamp, channel, identifier, dlc_text, payload = match.groups()
        raw_id = int(identifier, 16)
        data = bytes.fromhex(payload)
        if len(data) != int(dlc_text):
            raise ValueError('DLC does not match payload length')
        remote, dlc = False, 0
    error = bool(raw_id & 0x20000000)
    if raw_id & 0xC0000000:
        raise ValueError('Unsupported encoded frame flags')
    return Frame(float(timestamp), channel, raw_id & 0x1FFFFFFF, data, error, remote, dlc)


def read_log(path: Path) -> list[Frame]:
    result = []
    for number, line in enumerate(path.read_text(encoding='utf-8').splitlines(), 1):
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        try:
            frame = parse_line(line)
            if result and frame.timestamp < result[-1].timestamp:
                raise ValueError('Timestamps must be nondecreasing')
            result.append(frame)
        except ValueError as exc:
            raise ValueError(f'{path}:{number}: {exc}') from None
    return result


def statistics_report(frames: list[Frame], bitrate: int = 500000) -> dict:
    if bitrate <= 0:
        raise ValueError('Bitrate must be positive')
    if len({frame.channel for frame in frames}) > 1:
        raise ValueError('Analyze one CAN channel at a time')
    if any(b.timestamp < a.timestamp for a, b in zip(frames, frames[1:])):
        raise ValueError('Timestamps must be nondecreasing')
    duration = frames[-1].timestamp - frames[0].timestamp if len(frames) > 1 else 0.0
    grouped = {}
    bits = 0
    stuffed_bits = 0
    for frame in frames:
        if frame.error:
            continue
        payload_bits = 0 if frame.remote else 8 * frame.dlc
        bits += 47 + payload_bits
        stuffed_bits += 47 + payload_bits + (33 + payload_bits) // 4
        grouped.setdefault(f'{frame.can_id:03X}', []).append(frame.timestamp)
    by_id = {}
    for identifier, times in grouped.items():
        intervals = [1000 * (b - a) for a, b in zip(times, times[1:])]
        by_id[identifier] = {'count': len(times), 'rate_hz': len(times) / duration if duration else None,
                             'mean_interval_ms': statistics.mean(intervals) if intervals else None,
                             'jitter_stddev_ms': statistics.pstdev(intervals) if intervals else None,
                             'max_interval_ms': max(intervals) if intervals else None}
    return {'frames': len(frames), 'duration_s': duration, 'bitrate': bitrate,
            'error_frames': sum(f.error for f in frames), 'remote_frames': sum(f.remote for f in frames),
            'estimated_load_percent': 100 * bits / bitrate / duration if duration else None,
            'estimated_load_upper_percent': 100 * stuffed_bits / bitrate / duration if duration else None,
            'by_id': by_id}
