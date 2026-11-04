"""A small YAML signal subset, using little endian bit numbering only."""
from dataclasses import dataclass
import math
from pathlib import Path

import yaml

from .logs import Frame


@dataclass(frozen=True)
class Signal:
    name: str
    start: int
    length: int
    signed: bool = False
    scale: float = 1.0
    offset: float = 0.0
    minimum: float | None = None
    maximum: float | None = None
    unit: str = ''

    def __post_init__(self):
        if not isinstance(self.name, str) or not isinstance(self.unit, str) or type(self.signed) is not bool:
            raise ValueError('Signal name, unit and signed flag have invalid types')
        if not self.name or type(self.start) is not int or type(self.length) is not int:
            raise ValueError('Signal name and integer bit positions are required')
        if self.start < 0 or self.length < 1 or self.start + self.length > 64:
            raise ValueError('Signal must fit in 64 bits')
        numbers = (self.scale, self.offset, self.minimum if self.minimum is not None else 0, self.maximum if self.maximum is not None else 0)
        if not all(type(x) in (int, float) and math.isfinite(x) for x in numbers):
            raise ValueError('Signal conversion and limits must be finite')
        if self.minimum is not None and self.maximum is not None and self.minimum > self.maximum:
            raise ValueError('Signal minimum exceeds maximum')

    def decode(self, data: bytes) -> float:
        if self.start + self.length > len(data) * 8:
            raise ValueError(f'{self.name}: insufficient payload')
        raw = (int.from_bytes(data, 'little') >> self.start) & ((1 << self.length) - 1)
        if self.signed and raw & (1 << (self.length - 1)):
            raw -= 1 << self.length
        return raw * self.scale + self.offset


@dataclass(frozen=True)
class Description:
    messages: dict[int, tuple[Signal, ...]]
    heartbeat_ms: dict[int, float]


def load_description(path: Path) -> Description:
    try:
        raw = yaml.safe_load(path.read_text(encoding='utf-8'))
        if not isinstance(raw, dict) or set(raw) - {'messages', 'heartbeats'}:
            raise ValueError('Expected messages and optional heartbeats')
        messages, names = {}, set()
        for message in raw.get('messages', []):
            if set(message) - {'id', 'signals'}:
                raise ValueError('Unknown message field')
            identifier = message['id']
            if type(identifier) is not int or not 0 <= identifier <= 0x7FF or identifier in messages:
                raise ValueError('Message id must be unique and 11 bit')
            signals = []
            for item in message['signals']:
                if item.get('endian', 'little') != 'little':
                    raise ValueError('Only little endian signals are supported')
                signal = Signal(**{key: value for key, value in item.items() if key != 'endian'})
                if signal.name in names:
                    raise ValueError('Signal names must be globally unique')
                names.add(signal.name)
                signals.append(signal)
            messages[identifier] = tuple(signals)
        heartbeats = {}
        for item in raw.get('heartbeats', []):
            identifier, period = item['id'], float(item['timeout_ms'])
            if type(identifier) is not int or not 0 <= identifier <= 0x7FF or not math.isfinite(period) or period <= 0:
                raise ValueError('Invalid heartbeat id or timeout')
            if identifier in heartbeats:
                raise ValueError('Duplicate heartbeat id')
            heartbeats[identifier] = period
        return Description(messages, heartbeats)
    except (ValueError, TypeError, KeyError, yaml.YAMLError) as exc:
        raise ValueError(f'{path}: invalid signal description: {exc}') from None


def decode_frame(frame: Frame, description: Description) -> dict[str, float]:
    if frame.error or frame.remote:
        return {}
    return {signal.name: signal.decode(frame.data) for signal in description.messages.get(frame.can_id, ())}
